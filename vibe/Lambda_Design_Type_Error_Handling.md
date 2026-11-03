# Lambda — Type and Error Handling Design

**Status:** initializer and whole-variable reassignment admission, validation
outcomes without implicit block exit, procedural results, uniform handler results, removal
of the optional success arm and `T^E`, ordinary-error unification, combined fallback,
filtered handlers/chaining, and explicit error-or-null classification are
ratified (USER, 2026-11-02); implementation is partial. Further
error-handling design rules will be settled in this document.
**Formal linkage:** S7.5.6–S7.5.7, S7.6.8–S7.6.9, S7.6.5v4, S16.2.2v3,
S16.2.6v2 (handler convenience and classification);
S7.4.1v2–S7.4.2v3, S7.4.5v2–S7.4.6v3,
S7.5.1v6, S7.5.3v3–S7.5.5, S11.4.2v3, S12.1.4v4, S12.3.4v2,
D5.2.1v5 (ordinary-error unification and union-only particular errors);
S7.5.1v6, S7.5.2v4 (initializer and reassignment acknowledgement);
S7.7.1v3, S7.7.2v2, S7.7.4v2, S7.7.5v3, S7.7.6v2, S7.7.7v4, S11.4.1v5, S11.4.6v3,
S11.4.7v3, S12.2.1v3, SI14v3 (validation and binding outcomes);
D2.8.3v3, D3.2.2v3, D3.2.3v3 (representation and recorded type facts);
S2.5.3v3, S2.5.4v3, S12.1.2v3 (procedural results);
S7.6.1v9, S7.6.7v5 (uniform handler results);
S7.6.2v5, S7.6.6v6, S10.1.7v4, S16.4.3v4 (single-arm handler surface).
**Scope:** this is the home for the new error-handling design. Earlier decisions
remain in [Type Enforcement](Lambda_Design_Type_Enforcement.md), pending their
move as the remaining design questions settle. This record extends TE-13,
TE-16, and TE-18 as TE-13.1–TE-13.3, TE-16.1–TE-16.4, and TE-18.1–TE-18.2; it introduces no new ledger
series. Open questions below are discussion topics, not additional rulings.
**Related:** [formal semantics](../doc/Lambda_Formal_Semantics.md),
[procedural design](Lambda_Design_Procedural.md#24-return-and-block-results),
[runtime error handling](Lambda_Design_Runtime_Error_Handling.md).

## TE-13.1 — Initializer acknowledgement by type admission

**Decided 2026-11-02; runtime failure behavior revised by TE-18.1 the same day.
Authority: S7.5.1v6, S7.5.2v4. Core implemented; caller migration and aggregate qualification in progress.**

### Decision

For `let a = e`, a binding acknowledges the initializer's possible error when
its inferred receiving type admits that error outcome. The programmer need
not repeat `| error` in an annotation merely to acknowledge what inference
already knows. The same rule applies to an explicit binding annotation and
to initialization of `var`. TE-18.2 extends these rules unchanged to
whole-variable reassignment (S7.7.4v2).

Acknowledgement means accepting the outcome as a possible bound value. It
does not establish success, recover, or propagate out of the enclosing
procedure. An error accepted by the binding remains an ordinary error value;
subsequent statements can execute. The resulting type preserves the error
possibility, independently of its origin (S7.4.1v2–S7.4.2v3, TE-13.2).

**`any` definitely admits error** (S11.4.3); it is not an undecidable case.
`any` and `any | error` acknowledge identically. Admission follows the meaning
of a type, including aliases and equivalent union forms, rather than whether
the annotation contains the literal word `error`.

For example, given `f()` with declared return `int^`, the following bindings
are accepted by the new rule:

```lambda
let a = f()                 // infer int | error; retain either outcome
let b: any = f()            // any already admits the error
let c: int | error = f()    // explicit outcome type
let d: int^ = f()           // equivalent in every type position
```

This decision concerns initializer bindings. It does not grant an initializer
permission to hide unrelated calls inside nested functions or statements.
Outcome-preserving composition follows S7.5.4, without scope-wide
acknowledgement. Whole-variable reassignment now uses the same admission and
warning rules (TE-18.2); extension to parameters, returns, and container stores
remains a follow-up under their existing contracts.

### Static admission and uncertainty

For a known fallible initializer, check whether the receiving type admits its
possible error outcomes. Admission of one error subtype does not prove
admission of every possible error. Ordinary compatibility checks on the
success outcome also remain in force.

| Receiving type / inference state | Diagnostic and behavior |
|---|---|
| Proven to admit the possible error outcomes | Accept without an acknowledgement warning; preserve success or error. |
| Proven to exclude a possible error outcome | Static error. Recover, propagate, or widen the receiving type. |
| No annotation; inference lacks precision | Infer a conservative error-admitting type, potentially `any`. Accept; imprecision alone does not require an acknowledgement warning. |
| Explicit contract; error admission cannot be decided statically | **Warning, not a static error.** Keep the contract, check at runtime, and bind the admitted value or validation error (TE-18.1). |

Insufficient inference must never turn a possibly erroneous result into a
proven error-free value. Conversely, no acknowledgement diagnostic is needed
for an initializer proven error-free; ordinary type checking still applies.

For an undecidable explicit contract, a warning permits compilation but does
not assert that the value satisfies the contract. The runtime check yields
the admitted value or a validation error, which is bound as an ordinary
outcome. Subsequent code runs; the binding's effective type includes the
failure possibility (S11.4.1v5, S7.7.2v2). There is no substitution of `null`,
unchecked value, or stale binding, and no implicit exit from the block.
TE-18.1 replaces the earlier warn-then-skip fallback.

Suggested diagnostic wording (the diagnostic code and final wording remain
to be chosen):

> This initializer may produce an error, but the receiving contract cannot be
> proven to accept it; runtime validation may bind an error outcome.

The diagnostic should identify the binding and contract, and suggest an
error-admitting annotation, a handler, or propagation where permitted.
`T | error` is one way to make admission explicit, not compulsory syntax.

### Alternatives considered

Both options below accept a proven error-admitting receiver and reject a
proven incompatibility. They differ only when error admission is undecidable.

| Option | Benefit | Cost | Decision |
|---|---|---|---|
| **Static error:** require a provably admitting contract such as `T \| error`, or an explicit handler/propagation | Every accepted initializer has statically established acknowledgement; the author must resolve ambiguity. | Makes valid dynamic or computed contracts depend on the compiler's proof precision; adds annotations even when the runtime contract would admit the value. | **Not selected.** |
| **Warning:** allow compilation and enforce the explicit contract at runtime | Fits Lambda's proven/rejected/deferred contract model (S11.4.1v5), preserves dynamic use, and avoids redundant annotations. | A deferred failure is an error outcome that subsequent code must account for; retaining it does not ensure it is inspected. | **Selected, USER 2026-11-02; continuation follows TE-18.1.** |

The decision is to warn on unresolved admission, not to downgrade known type
conflicts or every unacknowledged call to warnings. It also avoids treating
equivalent types differently: adding `| error` to `any` supplies no additional
type information and must not be required as a syntactic acknowledgement.

### Consequences and acceptance cases

Given `f(): int^`, and an explicit contract `C` whose error admission cannot
be decided statically:

| Initializer | Required behavior |
|---|---|
| `let a = f()` | Accepted; inferred `int \| error`. |
| `let a: any = f()` / `let a: any \| error = f()` | Same acknowledgement and runtime outcome; no acknowledgement warning. |
| `type Outcome = int \| error; let a: Outcome = f()` | Same acknowledgement as the expanded union. |
| `let a: int = f()` | Static error: the known error possibility is excluded. |
| `let a: C = f()` | Warning; bind the admitted value or validation error and continue. |
| `let a = f() ^ { 0 }` | Accepted; recovery can supply a successful value. |
| `let a = f()^` | Propagates on error where the enclosing return contract permits it. |
| `let a = f(); later()` | An admitted error is bound to `a`; `later()` can still run. |

The last case is deliberate: **preserving an outcome is not proving success**.
An optional lint for an unused fallible binding may be useful, but its adoption
and scope remain open. No new must-inspect rule is introduced here.

## TE-13.2 — One ordinary error outcome and acknowledgement rule

**Decided 2026-11-02, USER. Authority: S7.4.1v2–S7.4.2v3,
S7.4.5v2–S7.4.6v3, S7.5.1v6, S7.5.3v3–S7.5.5, S11.4.2v3,
S12.1.4v4, S12.3.4v2; representation: D1.4v4, D5.2.1v5.
Core implemented; caller migration and aggregate qualification in progress.**

### Decision and rationale

Unify raised and returned **ordinary language errors**. An error constructed
by `error(...)`, returned by a function, returned early by `raise`, or produced
by failed validation is the same kind of outcome. All possible ordinary-error
outcomes require static acknowledgement under the receiving-context rules;
there is no soft category exempt from that obligation.

The caller should not need to know which producer spelling created an error.
Type admission already describes whether a binding, return, or container can
carry it. Explicit propagation describes early exit, while a handler describes
recovery. This removes the separate raised/returned classification without
removing convenient per-item error retention or introducing automatic exits.

### Construction, early return, and propagation

`error(v)` constructs a value. `raise v` evaluates its operand once, keeps an
existing error unchanged, or normalizes a non-error through `error(v)`, then
completes the enclosing callable early. Normalization uses the existing
constructor's code/message rules and raise-site location; an existing error
is not wrapped again or stripped of its payload (S7.4.4, S7.4.6v3).

```lambda
let err = error("failed")  // retain a value; subsequent statements execute
raise err                 // return that error early from the enclosing callable
```

In either `fn` or `pn`, `raise err` for an existing error has the outcome and
control effect of `return err`. `raise "failed"` means construct
`error("failed")` and complete the callable with it. **S7.4.6v3/S12.1.2v3**
also admit explicit `return` in an `fn` block body; this local-control
extension does not change error normalization, admission or acknowledgement.
The prior pn-only syntax restriction is superseded by
[Design_Procedural §2.4](Lambda_Design_Procedural.md#24-return-and-block-results).

The enclosing return contract must admit the error. A handler in the caller
can receive the call's error outcome. A handler body does not become a new
return target: `raise` and explicit `return` still exit the enclosing callable.
Postfix `e^` retains its success narrowing and returns an error early when
present (S7.6.3v3). Receiving an error in an ordinary binding does neither.

### Equivalent return spellings

**`T^` means `T | error` in every type position, including function returns.**
Equivalent annotations have identical admission and caller obligations, and
either permits `raise` when the error fits the return contract (S7.4.2v3).

```lambda
pn first() int^ {
    raise "failed"
}

pn second() int | error {
    return error("failed")
}
```

Both have the same outcome type and acknowledgement requirements. Swapping
their return spellings or using `raise` in the union-returning procedure does
not create a different category. Inference preserves error possibilities;
its inferred union has the same obligations as the equivalent written union.

This does **not** change the default unannotated-function return contract.
S11.4.2v3 still requires an error-possible return to be admitted by the
function's contract; no new inferred-fallible-return policy is introduced.
Plain successful `T` cannot silently return an error.

TE-13.3 below closes the typed-error-set question: remove `T^E`, retain `T^`,
and express particular error outcomes using ordinary unions. All ordinary
errors must fit the contract; the former implicit broad-error allowance is
retired (S7.4.2v3, S11.4.2v3).

### Acknowledgement retains, handles, or forwards an outcome

Given `operation()` returning `int^` or equivalently `int | error`:

```lambda
let a = operation()             // infer and retain int | error
let b: int^ = operation()       // explicit retention
let c = operation() ^ { 0 }     // recovery supplies a success value
let d = operation()^            // propagate where the enclosing return admits error
```

All four forms acknowledge. A `match` with an error arm also acknowledges.
Error-admitting parameters, returns, and container destinations retain their
existing admission contracts (S7.5.1v6, S7.8.1). Batch collections can retain
per-item errors; unification adds no fail-fast iteration rule.

**Acknowledgement is not mandatory inspection.** An inferred binding can
retain an error and execution can continue without examining it. `any`
already admits error. TE-13.1's explicit-contract uncertainty policy remains:
known incompatibility is a static error; undecidable initializer admission
warns, validates at runtime, and binds the admitted value or validation error.
Inference imprecision alone does not produce an acknowledgement warning.
The warning allowance is not generalized to every unresolved receiving site.

`operation() ^ { ^ }` now returns the same ordinary error. It has no hard-to-soft
conversion role. A handler's recovery result, including a returned error,
must fit its receiving context; a fresh error is not caught again by that
same handler (S7.6.1v9, S7.5.4).

### Composition and procedural overwriting

Check where error outcomes go, rather than demanding a separate handler at
every internal expression node. If an operation preserves an operand error
in its own outcome, acknowledgement can flow through that composition:

```lambda
let n = int(text) + 1  // retains the composed int | error outcome
```

The type must retain the error alternative. Arithmetic does not itself
acknowledge or recover the error; the receiving binding acknowledges the
outcome. This is not scope-wide permission: unrelated nested statements,
calls whose errors are dropped, and nested function bodies need their own
receiving contexts. Operations that transform or consume error-bearing data
still obey their own contracts, rather than an assumed propagation rule.

A known fallible result cannot instead be silently overwritten:

```lambda
pn example() int {
    operation()  // static error: a possible error outcome has no acknowledgement
    42           // would overwrite the preceding result
}
```

Capture, handle, or propagate that outcome. An error-admitting return
annotation does not acknowledge the non-final call. A final result can be
acknowledged by its actual return contract. No-result declarations leave the
preceding result intact, so syntactic statement position alone does not
decide whether the outcome is overwritten (S12.1.2v3, S7.5.5).

This check does not add runtime control flow. It does not restore declaration
skipping, and it does not diagnose an already captured binding merely because
the program never reads it. An optional unused-fallible-binding lint remains
separate.

### Boundaries retained and acceptance cases

Exceptional system faults such as OOM and stack exhaustion retain **S7.4.3**
and **S7.11**; they are not made ordinary typed outcomes by this decision.
`fn`/`pn` effect restrictions remain, but colour no longer chooses a different
ordinary-error convention, including dynamic `call` (S12.3.4v2).
Element/field mutation rejection/reporting and no-invalid-write contracts remain.
TE-18.2 separately extends continued outcome binding to whole-variable reassignment.

The physical ABI is an implementation choice under D1.4v4/D5.2.1v5. Boxed
outcomes and proven native success/error lanes can represent the same semantic
outcome. No new exception unwinding, result object, or native error sentinel
is introduced, and equivalent type spellings cannot change caller behavior.

| Case | Required behavior |
|---|---|
| Existing error via `raise err` vs `return err` in a `pn` | Same returned payload and early completion; no double wrapping. |
| `raise "failed"` vs constructing an error alone | The former constructs and exits; the latter only produces a value. |
| Function returns `T^` vs `T \| error` | Same admission, acknowledgement, and permission to raise an admitted error. |
| Error from a builtin, explicit return, `raise`, or deferred validation | Same ordinary-error acknowledgement discipline. |
| `let n = int(text) + 1` | Retain the composed error possibility and acknowledge at the binding. |
| Non-final fallible result overwritten without acknowledgement | Static error; capture, handle, or propagate. |
| Fallible result followed only by no-result declarations | Remains the block result; check its actual receiver. |
| Captured outcome never inspected | Allowed; optional unused-binding lint remains open. |
| `e ^ { ^ }` | Retains the error; never softens or erases its possible type. |
| Unresolved explicit initializer admission | Warning plus runtime validation and continued outcome binding. |
| `call` of `fn` vs `pn` | Same ordinary-error discipline; effect restrictions still differ. |

The retired rationale and exact prior rulings are retained in
[Appendix S](#raised-versus-returned-errors-retired-2026-11-02), along with the
moved TE-13 decision record. These are design history, not alternate current
semantics.

## TE-13.3 — Particular error outcomes use ordinary unions

**Decided 2026-11-02, USER. Authority: S7.4.2v3, S11.4.2v3;
representation: D5.2.1v5. Core implemented; aggregate caller qualification in progress.**

### Decision

Remove `T^E`. Keep `T^` solely as shorthand for `T | error` in every type
position. Ordinary unions are the sole mechanism for specifying particular
error outcomes, with the same admission and acknowledgement rules for raised
and returned errors.

| Contract | Admitted outcome |
|---|---|
| `T^` / `T \| error` | Successful `T` values or any ordinary error. |
| `T \| E` | Values of `T` or the particular error type/pattern `E`. |
| `T \| E1 \| E2` | Values of `T` or either specified error alternative. |
| `E` / `none \| E` | Only the specified error outcome, when `E` classifies error values. |
| `T^E` | Retired type syntax; use an ordinary union or broad `T^`. |

The union has its ordinary meaning: an `E` used to describe a particular
failure must classify error values. Putting a map or another non-error type
in a union does not turn those values into errors. An alias `type E = error`
still admits all ordinary errors; it creates no distinct error identity.
Filtered handlers use the same existing type/pattern classification for `E`
(S7.6.9), not a separate raised-error set.

### No hidden ordinary errors

`T | E` describes the actual admitted outcome. There is no implicit
`| error` added for builtin failures, conversions, deferred validation, or
another ordinary error producer. A possible ordinary error outside `E` must
be handled locally, converted to an error admitted by `E`, or disclosed by
widening the return contract. Broad disclosure is `T^`.

An explicit `raise err`, an ordinary error return, and a propagated error
obey the same contract. Known incompatible returns are static errors under
S11.4.2v3; a narrower signature cannot hide a broad-error producer. Existing
inferred/deferred admission policies for receiving positions remain as ruled;
this change does not add inferred fallible function returns or a new default
return contract. Exceptional system faults remain outside ordinary function
types under S7.11.1v2.

### Rationale and migration

The separate `T^E` notation once described the raised channel. With that
channel retired, it duplicates union types and preserves an origin-dependent
exception: `E` previously constrained user-raised errors while unrelated
ordinary defects could still escape. Removing that exception makes the
written error alternatives truthful and lets handlers, inference, and admission
use the same type machinery.

Retaining both `T^E` and `T | E` as equivalent spellings was considered and
rejected: it adds syntax without adding meaning. Retaining `T^` remains useful
for the common broad-error outcome and the requested binding-style cleanup.

Do not mechanically replace every old `T^E` with `T | E`: that can narrow a
contract which previously admitted other ordinary failures implicitly.
Inventory its actual outcomes. Use the union when every possible ordinary
error fits; otherwise recover/convert additional errors or use `T^` to preserve
the broad outcome. Existing task/stream descriptions that did not enumerate
specific error alternatives use `T^`; this does not invent new specific error
sets for those APIs.

Reject the removed form in return/value annotations and function types rather than
silently parsing the trailing `E` as a body or another statement. Keep ordinary
union grouping, `T^`, postfix expression propagation, and `e ^E { handler }`
filtered handlers; expression handlers are not typed-caret annotations.

### Acceptance cases

| Case | Required behavior |
|---|---|
| Return contract `T \| E`, error fitting `E` raised or returned | Same admitted outcome and caller acknowledgement. |
| Known possible error outside `E` reaches that return | Static rejection; handle, convert, or widen. |
| `T^` vs `T \| error`, including aliases | Same broad ordinary-error contract in bindings and signatures. |
| Ordinary error-only return contract `E` | Allowed when the actual outcomes fit; no `none^E` needed. |
| `type E = error` | Broad error alias, not a nominal error subtype. |
| Union contains a non-error map alternative | Map remains an ordinary value, not an error recognized by a handler. |
| Typed-caret form in return/value annotations or a function type | Syntax rejection on both parsers; migration guidance names unions. |
| Union propagation and filtered recovery | Retain residual error alternatives and check the actual receiver. |
| Exceptional system fault | Existing S7.11 policy; no hidden ordinary-error union alternative. |

The superseded convention and exact prior rulings are retained in
[Appendix S](#typed-caret-error-sets-retired-2026-11-02).

## TE-16.1 — Handler results follow procedural sequencing

**Decided 2026-11-02. Authority: S7.6.1v9, S7.6.7v5, S12.1.2v3.**

### Normal results, no result, and control transfer

A procedural block evaluates statements in sequence. Each normally produced
result replaces the preceding result, including `null`. A statement whose
contract produces no result contributes nothing and leaves the preceding result
unchanged. On normal completion the block yields the most recent result, or
`null` if none was produced. A procedure reaching the end of its body returns
that value.

**No result is not a value, not a new unit type, and not another spelling for
`null`.** Declarations produce no result (S2.5.4v3). A value-producing call that
returns `null` does replace an earlier value. Assignment and loop result
contracts are not redefined by this decision: sequencing consumes whatever
normal result their contracts provide, and does not infer a contract from a
runtime AST classification.

The statement that a standalone handler "discards its result" is therefore
wrong. The result can be replaced by a later result, or become the final result
of the block. The distinction matters even without a surrounding binding.

`return e` is a control transfer out of the enclosing callable, including when
written inside a handler or loop. It does not mean "produce the handler's
fallback". `raise`, `break`, and `continue` keep their ordinary control-flow
targets. None is a normal result update.

### One handler result rule for `fn` and `pn`

The operand is evaluated once. In the single error handler, a successful result
passes through unchanged; an error selects the handler body, whose normal
result becomes the handler's result. Errors produced by the handler body
remain fresh outcomes, not inputs to the same handler again (S7.6.1v9).
The optional success arm is retired by TE-16.2 below.

This is independent of the operand's `fn`/`pn` effect. A `pn` handler is valid
in a binding or another value position within procedural context, as well as
in a statement sequence. The effect boundary still rejects a `pn` call from
`fn` context (S12.1.1v3). Suspension preserves the same result and control-flow
semantics (S7.6.7v5).

For a fallback that should become `response`, write
`let response = io.fetch(url, request) ^ { failed_response(^) }`.
Adding `return` before `failed_response(^)` instead exits the enclosing
procedure on failure; `response` is never bound on that path.

This removes the need to manufacture paired mutable success/error variables
just because a callee is a procedure. It also makes a final handler obey the
same implicit-return rule as a final ordinary call. There is no requirement to
keep every intermediate result alive: an implementation may eliminate storage
for a replaced result while preserving evaluation and control flow.

### Acceptance cases

All bodies below are in procedural context. For the handler cases, `ok()` is
a procedure that succeeds with `3`, and `bad()` raises an error.

| Body | Required result | Reason |
|---|---|---|
| `{ 11; 22 }` | `22` | later normal result replaces earlier result |
| `{ 11; 22; null }` | `null` | `null` is a result |
| `{ 11; let x = 22 }` | `11` | declaration contributes no result |
| `{ let x = 22 }` | `null` | block completes with no produced result |
| `{ 11; ok() ^ { 7 } }` | `3` | final handler preserves success |
| `{ 11; bad() ^ { 7 } }` | `7` | final handler supplies recovery result |
| `{ 11; bad() ^ { null } }` | `null` | recovery to null replaces earlier result |
| `{ bad() ^ { 7 }; 99 }` | `99` | a later result replaces the handler result |
| `{ let x = bad() ^ { 7 }; x + 1 }` | `8` | recovery produces the initializer value |
| `{ let x = bad() ^ { return 7 }; 99 }` | procedure returns `7` | explicit return exits before binding or continuation |

## TE-16.2 — One error handler; no success or finally suffix

**Decided 2026-11-02, USER. Authority: S7.6.1v9, S7.6.2v5, S7.6.6v6,
S10.1.7v4, S16.4.3v4. Implemented in M1.**

### Decision

Keep **`e ^ { handler }`** as the basic unfiltered error handler. TE-16.3
and TE-16.4 extend fallback and error selection below, retaining one body. Remove the
optional `~ { success_body }` arm. The old suffix is a syntax error to be
diagnosed and migrated; it does not become `finally`, a success hook, or an
unrelated trailing expression.

Evaluate `e` once. Every non-error value, including `null`, `false`, and `""`,
passes through unchanged. An error evaluates the handler body with local `^`
bound to that error; its normal result becomes the expression's result. A new
error from the body is a fresh outcome. `return`, propagation, and permitted
loop control retain their ordinary targets. These rules are uniform for `fn`
and `pn`, including suspension and final-statement results (TE-16.1).

A handler never establishes a current-value `~` binding. References to `~`
inside it continue to refer to the enclosing pipe, match, constraint, or view
context; the retired success arm's implicit-field context is removed.

### Why remove the success arm

Value-producing procedure handlers make the common success-capture workaround
unnecessary. The handler now has one purpose: recover from an error while
passing normal values through. General branching remains with `match`, which
already supports distinct processing of error and non-error values.

| Option considered | Assessment | Decision |
|---|---|---|
| Retain the optional success arm | Compact binary branching, but duplicates `match` and retains a special success-value context. | Not selected. |
| Repurpose `~ { ... }` as `finally` | Adds unconditional cleanup, which needs pending-result/control preservation and cleanup-failure precedence; would silently change existing success-only code. | Not selected. |
| Single handler; cleanup considered separately | Keeps error recovery small, retains ordinary value flow, and avoids changing the meaning of an existing suffix. | **Selected.** |

A true cleanup clause would also run after recovery and before a pending
`return` or propagated error completes. Its normal result would need to
preserve the protected outcome rather than replace it under ordinary block
sequencing; cleanup failure would need an explicit precedence rule. None of
those semantics is introduced by this ruling.

### Migration

For a handler whose former success arm only stored the successful value:

```lambda
next = operation() ^ { failed_state(^) }
```

For distinct error and success transformations, bind the outcome once and use
ordinary branching under S7.5.2v4 and S11.2.1:

```lambda
let outcome = operation()
match outcome {
    case error: recover(outcome)
    default: transform(outcome)
}
```

The latter keeps `transform` exclusive to original success. Applying it after
a recovery handler would also transform the recovered value; placing it inside
the protected operand could catch its own failures. Neither is an automatic
equivalent rewrite of the retired two-arm form. Migration must also preserve
control transfers and enclosing `~`/implicit-field references: a `match` arm
introduces its own current-value context, unlike the old error-handler body.

### Cleanup remains separate

General cleanup is an open design question, not a new handler branch or an
approved keyword. The existing scoped-resource rulings S12.4.2–S12.4.3 remain
in force pending a separate decision; their implementation is still partial.
Future discussion should start from cleanup cases that are awkward to model
as scoped resources, then settle scope, ordering, cancellation, and failure
precedence explicitly.

## TE-16.3 — Combined fallback and explicit error-or-null classification

**Decided 2026-11-02, USER. Authority: S7.6.8, S7.5.6, S7.5.1v6,
S7.6.5v4–S7.6.6v6, S10.1.7v4, S16.2.2v3, S16.2.6v2,
S16.4.3v4. Handler implementation complete in M1; general acknowledgement is implemented
in M2/M3 and aggregate caller qualification remains in progress. The `empty` keyword is rejected
below under S7.5.7; ordinary user-defined aliases remain available.**

### One fallback for an error or ordinary falsy value

Add **`expr or^ { fallback }`**. It combines error recovery and ordinary
falsy defaulting without changing either ordinary `or` or the falsy set.
Evaluate the operand once. If it is an error, `null`, `false`, or `""`, run
the fallback once; otherwise return it unchanged. The fallback result is
returned unchanged even when it is an error or falsy value. It is not fed
back into the same fallback operation.

```lambda
let response = operation() or^ { default_response() }
let count = parse_count(text) or^ { 0 }  // successful zero is retained
```

`0`, NaN, empty arrays/maps, and other truthy values do not trigger fallback
(S3.1v2). `or^` does not redefine errors as falsy: it explicitly covers both
error and falsy cases. Ordinary `or` continues to preserve an error without
evaluating its right operand (S7.5.3v3).

The body binds **`^` to the exact triggering value**. Its type may therefore
include an error, `null`, `false`, or `""`; an error-only field access needs
an appropriate check. Preserve the existing outer `~` and restore outer `^`
after a nested handler completes. There is no implicit current-value `~` and
no synthetic error manufactured for a falsy input.

```lambda
let result = operation() or^ {
    if (^ is error) recover(^) else default_value()
}
```

Body results, explicit `return`/`raise`, procedural sequencing, and suspension
follow the existing handler rules. A null result remains a real result;
explicit return exits the enclosing callable. A fresh error from the body
requires an acknowledging receiver. Existing system-fault boundaries remain;
this shorthand introduces no new fault category or recovery mechanism.

### Syntax and alternatives

`or^` is a contiguous operator with ordinary `or` precedence and left
association. Its mandatory opening brace stays on the operator's line;
line-start `or^` inherits binary continuation. A whole combined fallback
can be parenthesized when applying a subsequent tight postfix operation.

```lambda
a + b or^ { fallback() }      // protect the complete sum
(operation() or^ { default_value() }) ^ErrorA { recover(^) }
```

| Candidate | Decision and reason |
|---|---|
| `expr or^ { h }` | **Selected.** Reads as ordinary defaulting extended to errors; keeps one lazy body. |
| `expr ^or { h }` | **Rejected as an alias.** The existing tokens are propagation followed by `or`, which must retain that meaning. |
| Change plain `or` to recover errors | **Not selected.** Would erase the explicit distinction between ordinary defaulting and error recovery. |
| Desugar as `expr ^ { h } or h` | **Incorrect.** A falsy result from the first `h` could execute it again. |

### Reject comparison-like shorthand; use type classification

Reject **`=^` and `^=`** as error-or-null test operators. Use:

```lambda
expr is (error | null)
```

The operand executes once; the result is true exactly for an ordinary error
or null, and false for `false`, `""`, zero, and empty collections. This is
explicit error classification and acknowledges the tested outcome under
S7.5.6. It does not change ordinary equality's error-preserving contract
(S5.1.1v2), return a replacement value, or establish that the original value
was successful.

The longer expansion `expr is error or expr == null` can evaluate `expr`
twice. The union test avoids that problem using the existing type language.
The rejected symbols looked assignment-like and would need extra rules for
non-null right operands or errors encountered during comparison.

<a id="empty-keyword-discussion--open"></a>
### No `empty` keyword; user-defined aliases

**Decided 2026-11-02, USER. Authority: S7.5.7.** Reject `empty` as a
keyword or predefined shorthand for `error | null`. There is no pressing
need for another language-level name; users can define an ordinary alias:

```lambda
type empty = error | null;
value is empty
```

Within its scope, this alias has exactly the same meaning as the explicit
union, including the acknowledgement rule for `is (error | null)` (S7.5.6).
It adds no value category, truthiness rule, or special parsing. Without a
user declaration, `empty` has no predefined error-or-null meaning.

Reserving the name for this union would also suggest empty text and
collections, although `""`, `[]`, and `{}` do not belong to it. Errors carry
diagnostics rather than merely denoting absent content. A user-defined alias
makes the intended meaning explicit and scoped, without adding a global
meaning of emptiness or confusing it with no result, `none`, or falsiness.

## TE-16.4 — Filtered handlers and ordinary left-to-right chaining

**Decided 2026-11-02, USER. Authority: S7.6.9, S7.6.2v5, S7.5.1v6,
S7.6.5v4–S7.6.6v6, S10.1.7v4, S16.2.6v2, S16.4.3v4.
Handler implementation complete in M1; M2/M3 acknowledgement implemented,
with aggregate caller qualification in progress.**

### Select an error type or pattern

Add the postfix **`expr ^ErrorType { handler }`** form. The selector is a
named error type/pattern, resolved using the existing type and match model
(S11.2.1); it is not a new error-variable binder or equality comparison with
an error object. Named constrained types can distinguish error codes or other
properties. A plain alias `type ErrorA = error` matches all errors and does
not establish a distinct error kind.

```lambda
let result = operation() ^ErrorA { recover_a(^) }
```

Evaluate the operand once. Non-error values always pass through unchanged.
A matching ordinary error evaluates the body with `^` bound to that error;
a nonmatching error passes through unchanged without evaluating the body.
No miss returns from the enclosing callable or abandons a block.

Only the matched alternatives are removed from the protected operand's error
possibilities. Nonmatching and body-produced errors remain in the effective
result and require an acknowledging receiver. An inferred binding can retain
them; a clean receiving type needs complete recovery or explicit propagation.
When the checker cannot prove a pattern covers an error alternative, it must
retain the residual possibility instead of claiming full discharge.

The ordinary handler's value, control-transfer, scoping, and suspension rules
apply. `^` is error-valued here, unlike the broader trigger in `or^`; no new
`~` context is introduced. Selector matching reuses existing classification
semantics rather than inventing equality for error values.

The accepted surface includes the named selectors shown here. Wider inline
selector syntax is a follow-up; use named types/patterns rather than inventing
another inline grammar. This rule settles ordinary errors. Reified system
fault filtering needs an explicit follow-up under S7.4.3/S7.11; an unmatched
fault must not silently acquire ordinary-error continuation semantics.

### Chains are nested handlers

```lambda
let result = operation() ^ErrorA { recover_a(^) } ^ErrorB { recover_b(^) }
```

It associates as `(operation() ^ErrorA { recover_a(^) }) ^ErrorB { recover_b(^) }`.
The operation runs once. Each handler consumes the previous expression's
outcome, including a new error produced by an earlier recovery body. A
nonmatching original error continues along the chain unchanged. A final
unfiltered `^ { ... }` can recover remaining ordinary errors.

This is not a sibling-catch list restricted to the original outcome. Use a
single `match` when only the original error should select one branch. Chaining
adds no success arm and gives `~ { ... }` no new meaning.

### Acceptance cases

| Case | Required behavior |
|---|---|
| `e or^ { h }` for error/null/false/empty text | Evaluate `e` once and `h` once; `^` is the original triggering value. |
| `e or^ { h }` for zero/NaN/empty collection/other truthy value | Pass through; do not evaluate `h`. |
| Fallback returns an error or falsy value | Return it unchanged; never retry the same fallback. |
| `a + b or^ { h }` | Fallback covers the whole sum. |
| `e is (error \| null)` | One evaluation; true for errors/null only; explicit acknowledgement. |
| `=^` / `^=` / `^or` as proposed aliases | No new operators or alias meanings. Existing propagation then `or` remains. |
| Filtered handler receives success or nonmatching error | Pass through unchanged; body does not run. |
| Filtered handler receives matching error | Body runs once with error-valued `^`. |
| First recovery produces an error selected by the next handler | Next body runs; chaining sees the fresh outcome. |
| Residual unmatched errors reach a clean receiver | No false clean typing; require acknowledgement/admission. |
| Nested ordinary/filtered/combined bodies | Innermost `^` has the correct subject; outer `^` and `~` survive. |
| Handler/fallback body returns null or explicitly returns/raises | Preserve normal null results and enclosing-callable control targets. |


## TE-18.1 — Validation produces an outcome; declarations do not skip

**Decided 2026-11-02, USER. Authority: S7.7.1v3, S7.7.2v2,
S11.4.1v5, S7.5.2v4, SI14v3. Core implemented; caller migration and aggregate qualification in progress.**

### Decision and rationale

Remove the implicit block exit caused by a failed `let`/`var` initializer
check. Validation is a fallible expression: it produces an admitted value
or a rich error, rather than choosing a block to abandon. Explicit propagation,
`return`, and other ordinary control-flow constructs retain their meanings.

The previous automatic containment protected typed bindings, but it introduced
a separate flow alongside ordinary values and explicit error propagation.
It could surprise readers, abandon user-written recovery code, and treat a
declaration failure differently from an error in an ordinary expression. It
provided only partial automatic handling: other soft errors continued as
values. Removing declaration skipping makes these outcomes follow one value
flow and lets the user choose where to handle or propagate them.

### Strict admission with a deferred outcome

For `let a: T = val`, retain three static cases:

| Static admission | Required behavior |
|---|---|
| Proven compatible | Admit the value; no validation-error alternative is added merely because an annotation exists. |
| Proven incompatible | Static error; an invalid value is not accepted as `T`. |
| Cannot be decided | Warn and validate at runtime. Bind the admitted value or the validation error, and continue with the next statement. |

Uncertainty may concern the source value, the target type's runtime value, or
the admission relation between them. A valid runtime-dependent type is not an
unresolved name: a missing type declaration remains an error under S11.4.12.
The warning does not disable the annotation or replace it with an unchecked
inference result. Unannotated inference alone still does not require the
acknowledgement warning (TE-13.1).

**Strictness applies to admission; the effective binding type describes the
whole outcome.** If a deferred check against `T` can fail, the effective type
includes `T | error`, or a sound conservative approximation when `T` itself is
runtime-dependent. The compiler records the declared target and effective
type separately (D3.2.3v3). No invalid original value is passed off as a `T`;
failure yields an error instead. If `T` already admits error, the union need
not add a distinct alternative. A binding proven error-free may retain the
clean type and representation.

This is the deliberate combination chosen by the user: strict checking where
it can be decided, with an error-bearing outcome where admission must be
deferred. It is not a requirement that every annotation be proven before
compilation can proceed, and it is not permission to store an error in a
native `T` slot. Error-bearing outcomes remain represented safely until
discharged or proven error-free (D2.8.1, D2.8.3v3).

### Validation as an expression

Conceptually, a deferred declaration binds the result of `admit(T, val)`.
**`admit` here is metanotation, not a chosen builtin or new syntax.** The
validation-expression surface remains a follow-up. Its required behavior is
to evaluate the value once and yield an admitted value or rich validation
error, with no declaration-specific control transfer. It must compose with
the existing `^ { ... }` handler and postfix `^` propagation.

Validation is not an unrestricted conversion: it retains the existing
admission rules, including S11.4.5's numeric admission where applicable. An
explicit converter such as `int(value)` is an existing example of a fallible
expression, not the general validation syntax.

An initializer handler handles errors produced by its operand. It does not
automatically cover a later check performed by the surrounding annotation:
`let a: T = val ^ { fallback }` can still fail admission to `T`. Under this
decision that failure binds an error and subsequent recovery is reachable.
To recover from validation before binding a clean `T`, the validation itself
must be inside the handled or propagated expression.

### Continuation and block results

Given a parameter whose concrete type is only known at runtime:

```lambda
pn example(value) int | error {
    print("before")
    let n: int = value       // deferred: effective type includes error
    print("after")
    n ^ { 0 }
}
```

Under the new rule, `example(3)` prints both messages and returns `3`;
`example("oops")` also prints both messages and returns `0`. In the latter
case `n` holds the validation error and the final handler can recover from it.
The declaration receives the unresolved-admission warning; the example is a
required behavior, not a claim about the current executable.

A declaration still contributes **no result** to its surrounding sequence
(S2.5.4v3). Binding an error does not itself make that error the block result.
For example, with a dynamic invalid `value`, `{ 11; let n: int = value }`
retains `11`; `{ let n: int = value; n }` yields the error. User code must
return, inspect, recover from, or explicitly propagate an error it wants to
surface. Unused fallible outcomes remain a possible lint, not implicit exit.

| Case | Required behavior |
|---|---|
| Dynamic valid value admitted to `T` | Bind the admitted value and continue. |
| Dynamic value rejected by `T` | Bind a rich validation error and continue; never substitute `null`. |
| Failure followed by `a ^ { fallback }` | Recovery is reachable. |
| Failure followed by `a^` | Explicit propagation exits according to the enclosing return contract. |
| Failed declaration in a nested block or loop body | No implicit exit from the block, iteration, or loop. |
| Failed declaration followed by another declaration | The next initializer executes under the ordinary sequence rules. |

This initializer decision is extended unchanged to whole-variable reassignment
by TE-18.2 below. It does not authorize invalid writes to typed containers or entering a callee
with a rejected argument. S7.7.3's call-site failure remains an error value
of the call. Field/element-store failures and dedicated typed loop-variable
admission retain their own contracts pending follow-up.

## TE-18.2 — Whole-variable reassignment follows initializer admission

**Decided 2026-11-02, USER. Authority: S7.5.2v4, S7.7.4v2–S7.7.7v4,
S11.4.1v5, S12.2.1v3, SI14v3; representation: D2.8.3v3, D3.2.3v3.
Core implemented; caller migration and aggregate qualification in progress.**

### Decision

Extend the existing initializer admission rules **unchanged** to an assignment
of the whole value of a mutable variable, `count = e`. Evaluate `e` once and
apply the same admission decision as initialization. On normal completion,
replace `count` with the admitted value or rich validation error and execute
the next statement. An error outcome does not retain the old value, skip a
block or loop iteration, or implicitly return from the procedure.

| Receiving contract and static facts | Assignment behavior |
|---|---|
| No annotation | Infer the new outcome, including possible errors; the earlier initializer's inferred type is not a fixed contract. No warning merely for inference imprecision. |
| Explicit contract proven to admit the RHS | Store the admitted value or error without an acknowledgement warning. `any` admits errors; aliases and equivalent types behave identically. |
| Explicit contract proven incompatible | Static error, exactly as for initialization. A known `int \| error` source cannot be assigned to declared `int`. |
| Explicit admission undecidable | Warn and validate at runtime. Store the admitted value or rich validation error, replacing the previous value, and continue. |

Compatibility still covers both successful values and possible error subtypes.
`int^` admits an `int | error` outcome; it does not admit an arbitrary successful
string. Deferral does not disable the annotation or accept a rejected ordinary
value as `T`. The declared contract remains unchanged for every later assignment,
even when an earlier deferred check has made the effective type `T | error`.

### Capture acknowledges; later uses retain the possibility

The assignment's receiving variable acknowledges the possible RHS error just
as an initializer does (S7.5.2v4). There is no requirement to inspect it on the
next line or add a handler to every read. Subsequent expressions keep the error
possibility and obey their own receiving contracts (S7.5.4); assigning an error
does not make later operations succeed.

Given `dynamic_call()` with return type `int^`, in procedural context:

```lambda
var count = 0
count = dynamic_call()      // capture and acknowledge int | error
let next = count + 1        // retain and acknowledge the composed outcome
let safe = count ^ { 0 }    // explicit recovery is reachable
```

The same assignment is accepted for `var count: int^ = 0`, but is a static
error for `var count: int = 0`. A genuinely dynamic source whose admission to
`int` cannot be decided takes the warning-and-validation route:

```lambda
pn example(value) int^ {
    var count: int = 5
    count = value           // warn; effective outcome is int | error
    print("after")
    count ^ { 0 }
}
```

Required behavior: `example(3)` prints `after` and returns `3`;
`example("oops")` prints `after` and returns `0`. In the latter case `count`
holds a validation error, not the earlier `5` or the rejected string.
These examples specify the design; they do not claim current runtime support.

### Type facts, handlers, and scope

Effective types must reflect all reachable assignments, including branch joins
and loop back edges. A prior clean initializer or annotation cannot justify
unboxing a later error. A later clean assignment or valid narrowing can establish
error freedom on its path; joins must still include other reachable outcomes
(SI14v3, D2.8.3v3, D3.2.3v3).

`count = e ^ { fallback }` handles errors from `e`, not a subsequent admission
failure of the annotation. It receives no blanket diagnostic exemption.
Recovery from that validation is reachable through the stored outcome; recovery
before committing a clean value requires validation inside the handled expression,
whose public surface remains open. Likewise `count = e^` explicitly propagates
before assignment on an error path: the assignment does not complete. Normal
`return`, `raise`, and loop control inside an RHS handler retain their targets.

This decision covers whole-variable replacement only. Element/field stores
retain S7.7.6v2/S7.8.1 and their no-invalid-write contract; it does not allow an
error in a clean `int[]` element or typed field. Parameter admission and dedicated
typed loop-variable admission are unchanged. It creates no new permission for
module state or writes through captures (S9.1.4). Assignment statement-result
contracts are not redefined; S12.1.2v3 still distinguishes normal results from
no result.

### Why retire retained-old-value assignment failure

The old assignment rule combined a retained old value with implicit block exit
and diagnostic tiers based on the abandoned tail. It could prevent a handler
after the assignment from running and required a second boundary policy beside
initialization. The new rule exposes the attempted assignment's outcome in the
variable and uses one admission decision at both sites. Strict rejection remains
where incompatibility is known; the cost is that downstream code must account
for any error in the effective type.

The retired rules and their rationale are preserved in
[Appendix S](#earlier-reassignment-retention-and-exit-rules-retired-2026-11-02).

## Open design follow-ups

These topics await further decisions; they do not alter current rulings.

| Topic | Question to settle |
|---|---|
| Other receiving positions | Whole-variable reassignment now shares initializer admission (TE-18.2). Should the inferred admission and warning policy extend to parameters, returns, and container stores under S7.7.3/S7.7.6 and S7.8.1? |
| Error-union normal admission | S7.4.2v3 and S11.4.5 do not explicitly settle whether `T | error` applies T's normal conversion/reification or preserves a value already matching the union. Pending user ruling: `float^ = 7` and reordered `Record^` members expose tier differences. Apply the decision consistently to `T^` and its ordinary-union spelling. |
| Validation expression | Choose the surface for value-or-error admission, including runtime-dependent target types; define how it composes with handlers and propagation without a new block-exit mechanism. |
| Container mutation and loop bindings | Settle field/element-store failures and dedicated typed loop-variable admission; avoid silently treating a retained old element/field as a successful write. |
| Validation diagnostics | Specify how rejection of an existing error retains or wraps its payload, and how the effective outcome type is displayed when the target is runtime-dependent. |
| Detailed composition boundaries | S7.5.4 settles acknowledgement through error-preserving composition. Audit operation-specific error consumption and callbacks against their contracts; do not grant scope-wide acknowledgement. |
| Filter selector extensions | Named selectors are accepted. Settle wider inline syntax and filtering of reified system faults under the existing fault policy (TE-16.4). |
| General cleanup | Does scoped-resource cleanup need a separate construct? Revisit S12.4.2–S12.4.3 with concrete cases; do not reuse the retired `~ { ... }` suffix. |
| Uninspected captured outcomes | Is an optional unused-fallible-binding lint desirable? S7.5.5 already rejects unacknowledged fallible results that would be overwritten. |
| Diagnostics | E241 identifies undecidable explicit local admission. Review location/suppression ergonomics and proof precision; inferred `any` alone does not warn. |
| Packages and demos | Review the remaining paired bindings, manual error tests, and handlers for concrete friction; use those cases to guide further convenience rules. |
| Design-record migration | Move the remaining error-handling rulings from Type Enforcement with their rationale and superseded alternatives, retaining the TE IDs and formal-spec links. |

## Appendix A — Implementation status

The phased work and qualification criteria are recorded in
[Type and Error Handling Implementation Plan](impl/Lambda_Impl_Type_Error_Handling.md).
M1 completes handler forms/results. M2/M3 implement core admission and
ordinary outcomes; package/demo migration and aggregate qualification remain
in progress. It includes the requested `T | error` to `T^`
binding-annotation cleanup, continued whole-variable reassignment outcomes,
migration of callers that relied on skips or retained old variable values, and
success-arm and typed-caret retirement, ordinary-error unification, combined fallback, filtered
handlers/chaining, explicit classification, and caller migration.

### Handler convenience status

**2026-11-02, M1 implemented.** Production/reference parsing, shared AST,
interpreter, and MIR support `or^`, named/qualified filters, and left-associative
chains. Runtime selection and conservative residual types are qualified by the
M1 tests. Explicit `is (error | null)` classification uses the existing operator and
acknowledges its subject under the M2/M3 receiver checks.
S7.5.7 needs no keyword implementation: `empty` is an ordinary user alias.
See the [M1 record](impl/Lambda_Impl_Type_Error_Handling.md#m1-handler-outcomes).

### Typed-caret retirement

**2026-11-02, core implemented.** Production and reference type grammars reject
`T^E` with migration guidance. Ordinary unions supply the particular error
alternatives, with broad `T^` retained. Four authored broad contracts were
migrated; the parser gate passes C 461/461 and Tree-sitter 448/448. General
package/demo qualification is still in progress. See the
[M2/M3 record](impl/Lambda_Impl_Type_Error_Handling.md#m2m3--admission-and-ordinary-outcomes-in-progress-2026-11-02).

### Ordinary-error unification

**2026-11-02, core implemented; aggregate qualification pending.** Return
normalization derives error/ABI metadata from the ordinary outcome contract.
The acknowledgement walk tracks producers through preserving expressions to
actual receivers, including inferred bindings and collected Item slots.
Procedural result replacement rejects an unacknowledged overwritten outcome.
The non-error default of an unannotated `fn` is preserved. S11.4.6v3's base-only
constraint admission is also preserved; predicate enforcement is not added by
this change. Current package migration and remaining qualification are in the
[M2/M3 record](impl/Lambda_Impl_Type_Error_Handling.md#m2m3--admission-and-ordinary-outcomes-in-progress-2026-11-02).

### Success-arm retirement

**2026-11-02, M1 implemented.** Both parsers reject the old suffix, even across
lines; the AST and both execution tiers have no `value_body` storage or lowering.
510 authored uses were migrated to one-handler expressions, error checks, or
explicit branching. Parser conformance and existing handler goldens qualify
single evaluation, result preservation, and restored lexical subjects.
See the [M1 record](impl/Lambda_Impl_Type_Error_Handling.md#m1-handler-outcomes).

### Validation and reassignment outcomes

**2026-11-02, core implemented; caller migration and qualification in progress.**
S7.7.2v2/S7.7.4v2 checks now bind/store their admitted value or validation
error and continue. Declared targets remain separate from effective outcome
facts. Captures, loop back edges, borrow publication and native-lane planning
use those facts; an error never becomes an integer merely because its
initializer or target had an integer lane. Field/element stores retain their
own containment. No public validation-expression spelling is added.

**2026-11-03 follow-up (S11.4.1v5/S7.5.4):** a deferred return check
contributes its possible rich validation error to the call's effective outcome.
The successful-value admission target remains strict. Inferred/error-admitting
receivers retain that outcome; a known fallible call cannot initialize a clean
receiver. JIT pair forwarding must preserve return admission. This does not
make an unused interior failed binding a returned error, or treat an open
recursive inference as an ordinary-error producer.

View state setup is part of the view's pure activation (S12.1.3v2). An explicit
postfix propagation in a state initializer uses that view's error-admitting
return contract (S7.6.3v3), and exits before rendering the body. A clean return
contract still rejects it. The propagated outcome crosses the same return
admission as a body result. This does not change state-slot receiving rules.

**Carrier qualification (2026-11-03, S7.8.1/D2.4.1–D2.4.3):** a propagated
call consumes its producer's actual Item/native representation. A native tail
join applies the callable's return admission before opening a success lane;
an ordinary error is never decoded as an integer or pointer. The full MIR GC
corpus passes 351/351; aggregate and remaining caller/native-proof qualification
remain open in the implementation record. Scalar error-union admission reuses
identity proofs without dropping the native error channel. Current interpreter union admission
retains an already-conforming numeric member, while plain numeric contracts
perform their required conversion. Whether an error union should apply T's
normal admission is pending the ruling listed above (S7.4.2v3/S11.4.5). Native return selection
therefore requires the actual normal carrier across all returns and branches;
a success-type name alone is insufficient. The qualified MIR emission count
is 278/280; remaining checks are recorded without relaxing safety requirements.
S7.7.2v2 constructor qualification also covers empty counted arrays, nullable
float literal members and dynamic record fields: optimized admission retains
the error at the receiving binding and continues. The constructor shares the
call-admission join without changing field evaluation or procedural store
containment. The expanded tier/GC matrix and full MIR GC corpus pass.
S7.6.3v3/S7.7.2v2 counted fill qualification keeps producer propagation before
destination validation: `let a: int[2] = fill(n, 1)^` retains a length mismatch
at `a` and continues, while an invalid fill count propagates. Uncounted numeric
literal fill success has a static membership proof; runtime storage admission
is still required. The focused tier/GC matrix passes 256/256.
S7.6.3v3/D2.4.1 dynamic dispatch preserves its boxed outcome until propagation
checks it, including when a loop continuation calls an existing module
definition. The deterministic handoff regression checks both normal values
and errors under forced/poison GC (D8.1.1v18).
S11.4.6v3/D3.2.4v3 declarations use the same named-record reification gate
as assignments, arguments and returns, including a record inside `N?`.
Semantic membership does not establish its packed offsets. The native
optimization gate passes 55/55 with its original copy/share budgets.
S11.4.1v5/S11.4.6v3 concrete return values still require full admission when
their numeric kind differs from the target: `int^ => 1.0` admits an integer,
while `int^ => 1.5` produces an error under current runtime admission. The
shared return gate keeps matching scalar lanes and nullable outcomes without
revalidation. Boxed tails enter that gate as Items, avoiding a second boxing.
S7.6.3v3 boolean system-call producers also publish whether they boxed their
outcome before `^`; the normal bool type does not describe that earlier
physical representation. These fixes retain the pending union-policy question.
S11.4.8v2/S4.2.2/D8.3.1v2 exact raw scalar value-binder keys use the same semantic type as
runtime binding, rather than retaining a singleton literal. Selection follows
parameter admission: conversion-dependent static inference retains an
approximation, and scalar raw entries require full membership and the admitted
kind before bypassing the checked entry. S7.7.3/S9.1.2 method preparation also
keeps an incoming error as the call's outcome when its receiver has a snapshot.
S7.7.4v2/S9.1.2 boxed constructor outcomes also retain successful container ownership and
snapshots across procedure-method writes. D8.3.3 forward calls whose native
plan is not established use the stable public Item entry; a later native
specialization cannot reinterpret tagged arguments. The full LaTeX corpus
now matches its JIT golden after this boundary repair. A complete interpreter
sweep still reports legacy and vendor divergences and has not published new
manifests; detailed comparisons and qualification limits are in the
[implementation record](impl/Lambda_Impl_Type_Error_Handling.md).

Return-admission inference publishes resolved declared results before a body
checks recursive calls (S11.4.1v5). Closed module type results are also available
to forward calls; parameter defaults and dependent return slots retain their
declaration scope. A functional branch block with one produced result keeps
that result's admission proof through nested branches (S2.5.4v3); its open
execution carrier does not by itself introduce a validation failure. Equality
against `null` retains direct error propagation without inventing a traversal
of nested container errors (S5.1.1v2/S5.1.2v2).

### Initializer acknowledgement

**2026-11-02, core implemented.** Inferred receivers acknowledge their complete
outcomes; `any` admits error without an acknowledgement warning. E241 reports
undecidable explicit local admission, retaining the runtime check and its
failure outcome. A known incompatible source remains a static error. The same
rule applies to whole-variable reassignment. Focused tier/GC and diagnostic
qualification is recorded in the
[M2/M3 record](impl/Lambda_Impl_Type_Error_Handling.md#m2m3--admission-and-ordinary-outcomes-in-progress-2026-11-02).

### Handler results

**2026-11-02, M1 implemented.** Procedure handlers preserve operand/recovery
values in bindings and as final results, including suspension. `null` is a real
replacement result; declarations still contribute no result. Interpreter and
MIR use the same sequencing classification. Normal-result erasure and implicit
return of fresh recovery errors were removed while preserving procedure fault
protection. Focused tests cover explicit return, chained recovery, nested
subjects, and forced GC. See the
[M1 record](impl/Lambda_Impl_Type_Error_Handling.md#m1-handler-outcomes).

## Appendix S — Superseded rules

### Handler result rules

- ~~TE-16 statement form: a procedure handler discards its successful value
  and normal handler-body result; no handler-result type is required.~~
  Replaced by TE-16.1 / S7.6.1v5: both outcomes produce ordinary results.
- ~~S7.6.7v4: procedure handlers are statement-only; value-producing handlers
  over possibly-suspending procedures are compile errors.~~ Replaced by
  S7.6.7v5. The v4 rule (2026-10-05) allowed statement handlers to suspend;
  v3 had routed their outcomes through durable completion and caller state,
  with a non-local carve-out for faults after suspension. The current rule
  retains suspension transparency without the statement-only restriction.

The former S7.6.7v4 implementation note recorded Runtime_Async P1 on
2026-10-05: durable completions, handler fault states, and the carve-out were
removed; concurrency scripts passed 25/25 on both tiers, plain and under
forced GC. That qualification predates the new value-position result rule.

### Earlier suspension restriction (2026-08-06; superseded)

The following is the historical TE-16 argument, moved from Type Enforcement;
it describes the former native-stack recovery design, not the current runtime
or the S7.6.7v5 rule.

> The handler must eventually cover the **system-fault** channel, not only value-error handling.
> The retired destructure used `transpile_local_fault_expression` with a real
> `LambdaRecoveryFrame` and inline `sigsetjmp`; that implementation path and its
> `AstNamedNode.local_fault_safe` state are gone. The remaining design constraint is unchanged:
> the jump buffer records a machine context inside the *currently executing* JIT activation, and
> an `await` unwinds the native stack back to the scheduler, resuming later on a different frame.
> A buffer captured before the suspension points at stack that no longer exists. The new handler
> lowering must use `MayAwaitScan` to reject such operands rather than silently degrading capture.
>
> ~~**Decision: `e ^ { … }` over a possibly-suspending operand is a compile error.**~~ Keep it simple
> first. This is the existing restriction made *loud* rather than silent, so nothing regresses, and
> it reuses the `MayAwaitScan` already written.
>
> **Rejected: silently splitting the capability** (always catch value errors, catch faults only
> when the operand cannot suspend). The same syntax would catch OOM in one place and not another,
> decided by whether the operand happens to contain an `await` — the "same construct, two
> behaviours by invisible context" pattern this design exists to eliminate. Worse for `^ { }` than
> for the old destructure, because the handler is an expression that composes freely and
> `(await f()) ^ { … }` is the canonical async shape.
>
> **Follow-on, not now: segment the protected region per poll.** The frame never actually needs to
> span a suspension — split the region at each `await` into straight-line segments and arm one
> frame per segment. The handler label is stable (same function), so every segment lands in the
> same place, and no jump buffer ever crosses a poll. This is cheaper than "represent fault
> recovery across task polls" because it *avoids* crossing polls rather than supporting them; note
> too that a fault inside the awaited task belongs to that task's own recovery frame, and a fault
> after resume is on the new stack, which a re-armed segment frame covers. Going from compile
> error to allowed is backward-compatible, so no code written under the rejection breaks later.

### Earlier acknowledgement policy (2026-07-29; superseded for initializers)

~~S7.5.2: `any` and a bare inferred binding never acknowledge; an initializer
must textually engage the error possibility.~~ Replaced by **S7.5.2v2**:
semantic admission suffices, and unresolved explicit admission warns and defers.
The old rationale prioritized visible awareness; TE-13.1 instead permits
retaining the outcome through inference. Other receiving positions remain
subject to their existing rules pending a separate decision.

The following historical sections were moved from Type Enforcement. Their
initializer restrictions and the claim that raised errors cannot reach a
deferred initializer check are superseded; the original arguments are retained
for comparison, not as current rulings.

> **Acknowledgment forms (DECIDED 2026-07-29 — generalization ratified): must-handle =
> must-engage-explicitly, at the immediate expression.**
> E228 guards against *unawareness*, not against deferred branching — so it is satisfied whenever
> the enforcing call's result is received by a context that **textually** engages the error
> possibility:
>
> 1. a braced handler or postfix-`^` propagation;
> 2. a binding, parameter, or declared return whose explicit type admits `error` — `T | error`,
>    `T^` (equivalent in value positions), or `error` itself. The binding carries the outcome as
>    a union, and ordinary assignability already enforces engagement-before-plain-`T` for both
>    spellings — the §7.3 wrapper idiom in one step, and the batch idiom for collecting
>    enforcing-call outcomes. The declared-return form counts **only for the call in return/tail
>    position** (`return f()`, or the final expression) — see tightness below. **`any` never
>    counts** — it admits error but acknowledges nothing;
> 3. a `match` with a `case error:` arm.
>
> Bare `let x = a()` remains an error — Lambda stays stricter than Zig/Rust/Go, all of which
> accept an untyped capture. The §10.7 firewall backstops the demotion path: an acknowledged
> error still cannot silently escape a declared plain-`T` interface. Implementation: one added
> condition at the existing E228 site (`build_ast.cpp:5197` — "declared type explicitly admits
> error") plus a third suggestion in the diagnostic text.
>
> #### Common binding acknowledgments
>
> For ordinary variable binding, the three common forms are:
>
> ```lambda
> let value = expr ^ { ^ }       // preserve the soft error value
> let value = expr ^ { null }    // intentional fallback value
> let value: T | error = pn_call // explicit union binding
> ```
>
> The third form is an acknowledgment boundary under **S7.5.1**: success binds `T`, while a
> hard raised error is captured as a soft `error` value in `value`. It neither re-raises nor
> chooses a fallback. The first form uses the handler to preserve the error; the second
> acknowledges it and replaces it with `null`, per **S7.6.1v3/S7.6.2v3**. These forms replace
> the retired `let value^err = pn_call` split under **S7.6.5v2**.
>
> **Acknowledgment tightness (DECIDED 2026-07-29, user): tight everywhere for `^` — keyed on the
> form, not on fn vs pn.** The acknowledgment must be the **immediate expression surrounding the
> call**; never distant, never scope-level. In particular, a declared error-admitting return does
> *not* retroactively acknowledge non-tail calls:
>
> ```lambda
> pn p() T | error {
>     f()        // compile error if f is enforcing — discarded outcome, no acknowledgment
>     g()        // same — and f's unhandled failure could be the very cause of g's
>     return r   // the declared return covers only THIS expression, not the calls above
> }
> ```
>
> Rationale: the `^` channel is *designed* to demand explicit acknowledgment — primarily for pn
> code, where an unengaged failure invalidates every subsequent command (§7.3: "commands halt on
> failure"); fn may use `^` too, under the same discipline. No laxer rule for fn is needed,
> because the relaxed pattern **already has its own spelling**: a callee that wants callers to
> write `let a = b()` and let errors flow simply declares `T | error`. The two forms are the two
> intended design patterns — the callee author chooses the caller discipline by choosing the
> form:
>
> | Pattern | Declare | Caller experience |
> |---|---|---|
> | Urgent — must acknowledge | `T^` | engage at the immediate expression, everywhere |
> | Relaxed — value-flowing | `T \| error` | `let a = b()` is fine; the error propagates as a value |
>
> #### Acknowledgement taxonomy for the enforcing channel
>
> A raised `T^E` must be acknowledged at the immediate expression (TE-13 tightness) by exactly
> one of:
>
> 1. `match` with a `case error:` arm;
> 2. `e ^ { … }` — handle here, optionally with `~ { … }` as the explicit non-error arm;
> 3. `e^` — propagate;
> 4. a receiving position that textually admits error — `let x: T^ = e`, `let x: T | error = e`,
>    or a declared parameter/return of that shape.
>
> `let x = e` alone never acknowledges (stricter than Zig/Rust/Go, per TE-13), and — the ruling
> this section adds — **TE-15's skip does not acknowledge either.** Skip is automatic
> containment for *defects*; it is not user engagement, so it cannot discharge an obligation
> the user is required to make visible. Consequently a raised error is compile-time gated and
> never reaches the skip machinery.
>
> **The three regimes, stated once** (they are routinely conflated):
>
> | Class | Mechanism | Compile-time obligation |
> |---|---|---|
> | soft error values (`T \| error`) | flow as data — the batch idiom | none |
> | defects (failed boundary checks) | TE-15 skip to closest safe boundary | none (automatic) |
> | raised errors (`T^E`) | must be acknowledged at the immediate expression | E228 |

### Earlier declaration-skipping and guard-dominance rules (superseded)

~~A deferred initializer failure leaves its name unbound and exits the declaring
block; the successful declaration guard makes every subsequent use clean.~~
Replaced by **TE-18.1 / S7.7.2v2 / S11.4.1v4**: validation is a value-producing
check; deferred failure binds an error outcome and continues. **D2.8.3v2**
requires proof of error freedom before native use, not an implicit block exit.

The earlier design protected annotated native values with one dominating
guard and made a failed per-item declaration automatically become the iteration
result. It avoided per-use checks but also abandoned user effects and recovery
code. The revised design retains checked admission and error-free native
storage while making the outcome visible in the binding's effective type.
The full TE-15/TE-18 deliberation is retained in
[Type Enforcement's superseded appendix](Lambda_Design_Type_Enforcement.md#appendix-s--superseded-declaration-skipping-design).
These replaced formal records are preserved here for exact scope;
unchanged parameter and mutation clauses still apply at their own boundaries.

> - **SI14 — A binding's static type is never a lie.** If `x : T` is
>   readable, it holds a real `T`: failure either never reaches the binding
>   (establishment skips before it exists) or leaves it unchanged
>   (reassignment keeps the previous value) — never a null-for-failure
>   placeholder. Every error value is deliberate; discharge strips error
>   constituents from its success type; no boundary silently substitutes
>   `0`, `null`, or reinterpreted bits. (Fault *timing* is exempt from SI3v2.)
>   [S7.4, S7.7, S11.4]
>
> - **S7.5.2v2*** **Initializer acknowledgement follows type admission.** A
>   `let`/`var` initializer is acknowledged when its declared or inferred
>   receiving type admits the possible error outcomes. `any` includes error
>   (S11.4.3), so `any` and `any | error` acknowledge identically; aliases and
>   equivalent type expressions do not change admission. An inferred binding
>   preserves the error possibility, using a conservative type if necessary.
>   A proven incompatibility is a static error. If an explicit contract's error
>   admission is undecidable, **warn and defer to the ordinary runtime check**
>   (S11.4.1v3); failure never establishes the binding and ends its declaring
>   block with the boundary error (S7.7.2). Mere inference imprecision does not
>   require an acknowledgement warning. An admitted error is retained as a
>   value, so subsequent statements can execute. Containment itself is not
>   acknowledgement; the warning is the explicit allowance for unresolved
>   initializer admission. Other receiving-position rules are unchanged.
>   [TE-13.1]
>
> - **S7.7.1*** Skip is a **declaration-boundary mechanism only** — `let`,
>   `var`, `for` loop variables, declared parameters, declared returns.
>   **Expression interiors never skip**: in `a + e + b` the error flows as a
>   value and the expression types `T | error`. (Strict left-to-right
>   evaluation order is *not* normative.) [TE-18]
>
> - **S7.7.2*** A failed deferred check at a declaration skips to the end of
>   the block that **declares** the binding; that block yields the error; the
>   binding is never established, and code between failure and boundary never
>   runs. On success, the declaration's guard **dominates every use of the
>   binding in its scope** — native-lane, no per-use checks. [TE-18]
>
> - **S7.7.5*** In a `for`, the skip target is the **iteration body**, not the
>   loop: per-item skip keeps the batch alive; *body native, result boxed* —
>   the loop result is `(T | error)[]`. An accumulator declared outside the
>   loop dies with the batch, correctly (a stale accumulator would poison every
>   later iteration); a per-item temp declared inside it continues. *Batch
>   friendliness is a theorem, not a hope.* [TE-15, TE-18]
>
> - **S7.7.7v2** Recovery belongs in the initializer: `let a: T = e ^ { … }`.
>   Recovery after a failed declaration is unreachable by construction;
>   `or` in the initializer does not contain an error. [TE-15, TE-22]
>
> - **S11.4.1v3** An annotation is a contract on the binding, not a hint.
>   **Three outcomes, never a fourth**: statically proven, statically
>   rejected, or a deferred runtime check whose failure produces a rich error
>   (boundary, expected type, actual value, location) — never null, never a
>   wrong value, never silence. Failure never establishes the binding;
>   reassignment checks before commit and leaves the old value unchanged. A
>   `T[]` boundary validates/adopts every logical element before publishing its
>   representation, and a typed array mutation validates its new element before
>   changing the owner. An annotation may drive representation only if its
>   boundary is enforced. An implementation may reuse an admitted array's
>   representation proof only while the actual carrier matches that proof;
>   otherwise it re-admits before optimized access. This internal choice never
>   changes the contract, values, or failure behavior. [S7.7.2, S7.10.6,
>   D3.3.3v3, TE-1, TE-2, TE-4, TE-9]
>
> - **S11.4.6*** User-defined types are enforced by the validator: deep, on
>   first crossing, or a rich error with a validator path and no binding.
>   Named map types are **open** — extra fields pass. Constrained types
>   (`T that …`) enforce the base only, for now. [TE-10]
>
> - **S11.4.7** Containment and discharge follow §S7.7–S7.8: skip at
>   declaration boundaries, destination-contract container acceptance, and
>   `^ { }` as the engagement form that suppresses the skip.
>
> - **S12.2.1** `let` bindings and parameters are immutable; only `var`
>   reassigns; shadowing is allowed. An unannotated `var` may change runtime
>   type on reassignment; an annotated `var` constrains every assignment to
>   the declared type (S11.4.1). Implicit widening at annotated positions is
>   by **exact embedding** only (S4.4.1) — never lossy coercion; assignment
>   never silently corrupts data. [Design_Procedural §3]
>
> - **D2.8.3*** **Lane entry is gated on static proof, never on runtime
>   rescue.** Admission is read from the destination contract (§S7.8.1): a
>   provably infallible source enters branch-free; a source only inferable
>   as `T | error` is **not lane-eligible** and stays boxed until
>   discharged — never a checked lane store that might fail mid-mutation. A
>   declaration's entry guard then dominates every use of the binding in its
>   scope (§S7.7.2), so D2.8.1 and D2.8.2 hold **by construction rather than
>   by per-use guards**. The gating analysis is `may_defect` with
>   fail-closed polarity (D6.1.3): unknown ⇒ defect-capable ⇒ demotion,
>   which is viral through unanalyzed callers — so the effect split is a
>   **prerequisite** for lane routing, not a later optimization, and an
>   unannotated literal that boxes solely for unproven fallibility must be
>   diagnosed rather than silently demoted. [TE-17, TE-18; IEH I3, I4]
>
> - **D3.2.2*** The **validator is the runtime enforcer** for user-defined
>   types: deep, on first crossing, producing a rich error with a validator
>   path — or the binding never exists. Constrained types (`T that …`)
>   enforce the base only, for now; `is`, `fn_is`, the emitter, and the
>   validator must consolidate onto the shared foundation (a known
>   three-way divergence today). [TE-10, TE-6 P5]
>
> - **D3.2.3** Declared and effective types are **separate recorded facts**
>   (declaration on the binding node, inference beside it): an annotation is
>   a contract (semantics S11.4.1); inference is never a binding contract
>   and may never override what it proved. [TE-1, TE §8.1]

### Earlier success-arm design (retired by TE-16.2)

~~`e ^ { error_body } ~ { value_body }` provides an optional success-only arm
and binds the successful value to its local `~`.~~ Replaced by **S7.6.1v6 /
S7.6.2v4**: one error handler, non-error pass-through, and `match` for explicit
outcome branching. `~ { ... }` is not reinterpreted as cleanup.

The original six-option comparison, rationale, examples, and replaced formal
records below are history. Their success-arm syntax and local-current-value
context are no longer current rulings; remaining one-handler behavior is
preserved by TE-16.2.

> #### Extended two-arm handler syntax — six options and decision (decided 2026-08-17, user)
>
> The one-arm handler is ideal when success should pass through unchanged, but explicit
> error-versus-value branching is common enough to warrant a compact form. The six considered
> surfaces were:
>
> | Option | Surface | Assessment |
> |---|---|---|
> | 1 | `e ^ { error_arm } { value_arm }` | Rejected: two adjacent anonymous blocks are too cryptic; the second block has no visible role marker. |
> | 2 | `e ^ { error_arm } else { value_arm }` | Rejected: `else` creates dangling/ownership ambiguity beside `if … else`, especially when the handler is the `if` body expression. |
> | 3 | `e ^ { error_arm } default { value_arm }` | Viable runner-up: explicit and aligned with `match`, but `default` describes selection indirectly rather than naming the normal-value context. |
> | 4 | `e ^ { value_arm } error { error_arm }` | Rejected: the first block reverses the established meaning of `^ { … }`; its interpretation would depend on a later suffix. |
> | 5 | `e ^ { value_arm } catch { error_arm }` | Rejected: it has the same reversal and imports `try`/exception expectations into a channel-agnostic soft-or-raised error handler. |
> | 6 | `e ^ { error_arm } ~ { value_arm }` | **Decided:** preserves the established error arm, marks the non-error arm with Lambda's current-value symbol, and needs neither a new keyword nor `else` ownership rules. |
>
> **Decision: adopt option 6, `e ^ { error_arm } ~ { value_arm }`.** The operand is evaluated
> exactly once. A soft or raised error selects the first arm and binds it to handler-local `^`;
> every non-error result, including `null` and `false`, selects the second arm and binds it to
> handler-local `~`. In the error arm, an enclosing `~` keeps its existing meaning. In the value
> arm, the new `~` is innermost and shadows an enclosing current-value context. Errors raised by
> either selected arm are fresh outcomes and are not swallowed by the same handler.
>
> ```lambda
> pn_func(inner, options) ^ {
>     // error arm: ^ is the raised pn error
>     diagnostic(^)
> } ~ {
>     // value arm: ~ is the pn result
>     transform(~)
> }
> ```
>
> This statement-position shape is expected to be the common `pn`-call flow: handle a directly
> raised error in the first arm, or continue with the returned value in the second. Omitting the
> `~` arm preserves the one-arm rule: success passes through unchanged. In expression position the
> result type is `type(error_arm) | type(value_arm)`; in a procedural sequence
> the selected arm's normal result updates the block result before execution
> continues (**S7.6.1v5, S12.1.2v2**). The complete two-arm construct
> remains primary-like at the member-access/postfix tier, so, for example,
> `e ^ { h } ~ { v }.field` means `(e ^ { h } ~ { v }).field`.
>
> > **Implicit fields (S10.1.7v2, ruled 2026-09-25).** The value arm binds `~` to
> > one value, as a `match` arm does, so it is a single-subject body: a bare
> > field name reads `~.name` of the result, as in `find(id) ^ { 0 } ~ { total }`,
> > unless a binding claims the name. The error arm binds `^` and keeps the
> > outer `~`. Reasoning: [Expr_Pipe §F.7](Lambda_Expr_Pipe.md).
>
> `match` remains the long-term canonical and extensible branching form; the two-arm handler is
> compact syntax for the frequent binary outcome split:
>
> ```lambda
> match e {
>     case error { /* error branch; handler form exposes this value as ^ */ }
>     default { /* non-error branch; handler form exposes this value as ~ */ }
> }
> ```
>
> The semantic authority is **S7.6.1v3/S7.6.2v3** (with acknowledgment under **S7.5.1**).
> The grammar, AST, MIR lowering, nested handler contexts, and expression/direct-`pn` regression
> coverage landed on 2026-08-17.
>
> - **S7.6.1v5*** **`e ^ { … ^ … }` handles the error locally and is
>   channel-agnostic** — it receives soft values, raised errors, and (at `pn`
>   boundaries) system faults alike, binding the error to handler-local `^`
>   (innermost-wins). In the one-arm form, the handler introduces no `~`
>   binding: `~` retains the current-value meaning supplied by an enclosing pipe,
>   match, constraint, or view context, and a non-error operand passes through
>   unchanged. Typing removes the handled error alternatives:
>   `type(e ^ { h }) = (type(e) \ error) | type(h)`.
>
>   The optional two-arm form **`e ^ { h } ~ { v }`** evaluates `e` exactly
>   once. An error selects `h`, with `^` bound to that error and any enclosing
>   `~` left intact; every non-error value — including `null` and `false` —
>   selects `v`, with `~` bound to that value as the innermost current-value
>   context, so a bare field name in `v` reads `~.name` (S10.1.7v2). Its type is
>   `type(h) | type(v)`. An error raised while evaluating
>   either selected arm is a fresh outcome and is not consumed again by the
>   same handler. **The handler produces its normal result for both `fn` and
>   `pn` operands**, including when used as a standalone statement. In a
>   procedural sequence that result replaces the preceding result under
>   S12.1.2v2; a later result may replace it in turn. A final handler supplies
>   the block's result. A `null` result replaces an earlier value; it is not
>   suppressed. `return` inside an arm exits the enclosing procedure, not just
>   the arm; `raise`, `break`, and `continue` retain their ordinary targets.
>
>   In either form the selected handler result has the ordinary contextual type
>   or the arm does not complete normally (`raise` / `return`); the binding it
>   feeds is therefore **statically clean** — *sound by construction*, no flow
>   analysis: *a binding's static type is never a lie* (SI14).
>
>   Ordinary error outcomes always reach this handler by normal return and a
>   local generated branch; the handler never uses non-local recovery for a
>   returned or raised language error. A handler may protect a procedure call
>   that suspends (S7.6.7v5), with the same single-evaluation, result, and
>   fresh-outcome rules after resume. [TE-16.1, REH-D8, REH-D12]
>
> - **S7.6.2v3** The handler is the left-associative postfix-primary form
>   `primary ^ { error_body }`, optionally followed immediately by the normal
>   value arm `~ { value_body }`. It occupies the same precedence tier as member
>   (`.`) and query (`?`) access. Consequently `a + b ^ { h } ~ { v }` means
>   `a + (b ^ { h } ~ { v })`; handling the complete binary expression requires
>   `(a + b) ^ { h } ~ { v }`. The complete handler result is primary-like, so
>   postfix operations continue left-to-right: `e ^ { h } ~ { v }.field` means
>   `(e ^ { h } ~ { v }).field`, and handler chains associate from the left.
>   Both arms are brace-delimited; the optional `~ { … }` belongs to the handler
>   only when it immediately follows the error arm. `^` followed by anything
>   other than `{` is propagation (`f()^ - 1` propagates then subtracts). There
>   is no prefix `^ { h } e` shorthand and no `else`, `default`, `error`, or
>   `catch` spelling for the second arm. The caret belongs to the handler or
>   propagation construct, never to `call_expr`. [TE-16]
>
> - **S7.6.6v3** Division of labor: `or` defaults ordinary falsy values and
>   preserves errors;
>   `^ { }` catches errors only, with access and normal-value pass-through;
>   `^ { } ~ { }` branches explicitly between error and non-error outcomes;
>   `e^` catches errors only, propagating. Both caret forms remain
>   channel-agnostic; `or` acknowledges neither error channel. [TE-16, TE-22]
>
> - **S10.1.7v2** **A body about one subject may leave `~` implicit; the pipe
>   family always spells it.** A body that binds `~` to one subject, its
>   current item, may read a field of it by the field's bare name, the OOP
>   receiver convention. Such bodies are a method (S8.2.3), an object-level
>   `that` constraint and a type constraint `T that cond` in any position
>   (S11), the `that` proviso (S10.1.5v3), a `match` arm (S11.2.1), and a
>   handler's value arm `e ^ { h } ~ { v }` (S7.6.1v4).
>
>   So `user that age >= 18` and `match user { case map: age >= 18 }` both
>   read `~.age`, and `~` may still be spelled. The current item is scoped to
>   its body: a nested body binds its own, spelled or implicit, and when it
>   ends the outer binding is back. In
>   `order that (match kind { case map: total }) + total == 9`, the first
>   `total` is `kind`'s and the second is the order's.
>
>   Declared fields (a method, an object constraint) are names of the body's
>   scope and shadow outer bindings. Elsewhere the current item has no
>   declared shape, so a bare name is the member `~.name` (S12.3.3v2) only
>   when no binding claims it: not a scope name, an `import math` constant, or
>   a module or namespace prefix (`m.sqrt(x)`). An implicit read is a read
>   only. It never supplies a callee — `xs that len(~) > 2` calls `len`, not
>   `~.len` — and never a write target: `x = 1` in an arm assigns the
>   variable `x` or fails, and never stores into the matched value.
>
>   A pipe-family body (`|>`, `|:`) reads bare names as ordinary names, even
>   nested in one of these bodies. `|>` tells mapping from application by the
>   free `~` its text spells (S10.1.2v4), and `|:` must read that text the
>   same way (S10.1.6). A field supplied behind the text would also let a
>   misspelled `xs |: is_evne` pass E238 as a silent `[]`. For the same
>   reason, a `~` that a nested body binds, spelled or implicit, is never free
>   in an enclosing pipe body (S10.1.3): `xs |> match (1) { case int: ~ * 10 }`
>   is whole-value application, not a mapping. [Expr_Pipe §F.7]
>
> - **S16.4.3** **Declaration braces are structural and never read as maps.**
>   `fn`, `pn`, `view`, and `on` bodies, braced match arms (`case T { ... }`),
>   handler arms (`^ { ... }`, `~ { ... }`), and `while` bodies are always
>   blocks; `{}` in each is the empty body. `while` belongs here by the same
>   value-use principle — it is procedural-only, so its body value is always
>   discarded and a map body is dead by construction. Each declaration has an
>   expression escape where a value body is wanted (`fn f() => {a: 1}`,
>   `case int: {a: 1}`). `match`'s outer braces delimit the arm list
>   (S16.6.4); `type` bodies have their own interior; map-*type* patterns
>   (`{a: int}`) live in type space and are untouched. The dividing test
>   between S16.4.1v4 and this ruling: interior decides exactly where a
>   bare-expression second form exists; the brace is structural exactly
>   where it is mandatory — handler arms have no unbraced form, since
>   `expr ^ expr` is already propagate-then-continue (S16.2.6).
>   [Design_Syntax §5.9]
>
>   | Braces | Reading |
>   |---|---|
>   | value/expression position, call args, initializers (`fn` **and** `pn`) | map or block by interior; `{}` = map |
>   | `if`/`for` bodies (both spellings), `else`, colon-form match arms, arrow bodies | map or block by interior; `{}` by fn/pn context (arrows: always fn) |
>   | `fn` `pn` `view` `on` bodies, braced match arms, handler arms, `while` bodies | always block; `{}` = empty body |
>   | `match` outer braces | arm list (structural) |
>   | `type` bodies | fields/constraint/methods interior |
>   | content position | map item (`{}` = empty map item, meaningful) |
>   | bare statement position in `pn` | **error** (dead either way) |
>   | type-annotation `{...}` | type space, unaffected |

### Raised versus returned errors (retired 2026-11-02)

~~Ordinary errors have distinct soft/value and hard/raised categories; only
the latter require caller acknowledgement, and only a declared caret return
licenses `raise`.~~ Replaced by **TE-13.2 / S7.4.1v2–S7.4.2v2,
S7.4.6v2, S7.5.1v4–S7.5.5**.

The earlier design aimed to support two workflows: batch transformations that
retain per-item errors, and commands whose failures demand immediate attention.
It also kept inferred error unions from imposing new obligations on callers.
The distinction was made explicit in return syntax and the `raise` permission,
even though the same handler and propagation syntax worked on both forms.

It is retired because those workflows do not require different kinds of error.
An error-admitting binding or container retains a batch outcome; a handler or
postfix propagation expresses recovery or early return. Inferred binding
acknowledgement already supplies the convenient capture path. Different rules
for equal-looking values added return-position exceptions, producer-dependent
caller obligations, and identity-handler wrappers whose only purpose was to
soften an error. The new rule keeps the workflows, checks outcomes uniformly,
and preserves the separate runtime policy for exceptional system faults.

The following are exact prior formal records, preserved as historical text.
Their soft/hard distinctions, colour-dependent error convention, and
spelling-dependent return rules are superseded; unrelated clauses retain the
scope of their current formal successors.

> - **S7.4.1** **Value errors `T | error`** — soft; flow as data; no caller
>   obligation; detected at type boundaries. This is the only form inference
>   produces. *Interiors flow, interfaces enforce.*

> - **S7.4.2** **Raised errors `T^` / `T^E`** — enforcing; explicit-only
>   (inference never creates `^`); must be engaged at the immediate expression;
>   alone license `raise`. `raise` in a `T | error` fn is a compile error —
>   construct and return `error(...)` instead. *`^` raises; `| error` returns.*
>   In value positions `T^` ≡ `T | error`; `^` is semantically distinctive only
>   on function returns. Every `R^E` is operationally `R^(E | error)`: `E`
>   constrains user-raised errors, system defects flow implicitly.

> - **S7.4.5** System `fn` failures are values, never `T^E`: *absence in / no
>   answer → `null`* (or `""` for string results); present-but-invalid →
>   `error()` (§S7.10). `input`/`fetch` are effectful readers — pn-family, they
>   **raise** (`T^E`), though permitted in expression position; set-oriented
>   input is an explicit wrapper (`fn my_input(f) { input(f) ^ { ^ } }`). The
>   handler is the acknowledgment boundary: `^ { ^ }` engages the hard raised
>   error and returns it as a soft `error` value (`T | error`).
>   [C14, C14a]

> - **S7.4.6** `raise v` with a non-error `v` is shorthand for
>   `raise error(v)`: the constructor (S7.4.4) decides the code and message,
>   and the error is stamped where `v` is written. An error operand is raised
>   unchanged. *`raise` always raises an error.* [TE-21]

> - **S7.5.1v3*** A call carrying a `^` channel must engage the error at the
>   **immediate expression**, through exactly one of: an `error` arm in
>   `match`; the `^ { }` handler; postfix `^`; an initializer binding under
>   S7.5.2v3; or a declared parameter/return explicitly admitting error
>   (`T^`, `T | error`, or `error`). *Acknowledgement can preserve the outcome;
>   it need not recover or prove success.* Logical operators do not themselves
>   acknowledge raised errors. The initializer rule does not grant scope-wide
>   acknowledgement to unrelated calls. [TE-13.1, TE-16, TE-22]

> - **S7.5.3v2** `a or b` defaults only `null`, `false`, and `""`; an error
>   from `a` remains the expression's error and `b` is not evaluated (S3.4).
>   Its type must retain that error possibility: `or` never narrows away error
>   or discharges a raised channel. Recovery is explicit, as in
>   `int(s) ^ { 0 }`; ordinary defaulting after propagation is `a()^ or b`
>   where the enclosing function permits propagation. [TE-22]

> - **S7.6.1v6*** **`e ^ { … ^ … }` is the single postfix error handler and is
>   channel-agnostic** — it receives soft values, raised errors, and (at `pn`
>   boundaries) system faults alike, binding the error to handler-local `^`
>   (innermost-wins). The operand is evaluated exactly once; the handler
>   introduces no `~` binding: `~` retains the current-value meaning supplied by an enclosing pipe,
>   match, constraint, or view context, and a non-error operand passes through
>   unchanged, including `null`, `false`, and `""`. Typing removes the handled
>   error alternatives:
>   `type(e ^ { h }) = (type(e) \ error) | type(h)`.
>
>   An error selects the handler body; its normal result becomes the expression's
>   result. An error produced by the body is a fresh outcome and is not consumed
>   again by the same handler. **The handler produces its normal result for both
>   `fn` and `pn` operands**, including when used as a standalone statement. In a
>   procedural sequence that result replaces the preceding result under
>   S12.1.2v2; a later result may replace it in turn. A final handler supplies
>   the block's result. A `null` result replaces an earlier value; it is not
>   suppressed. `return` inside the body exits the enclosing procedure, not just
>   the handler; `raise`, `break`, and `continue` retain their ordinary targets.
>
>   The handler result has the ordinary contextual type
>   or the body does not complete normally (`raise` / `return`); the binding it
>   feeds is therefore **statically clean** — *sound by construction*, no flow
>   analysis: *a binding's static type is never a lie* (SI14).
>
>   Ordinary error outcomes always reach this handler by normal return and a
>   local generated branch; the handler never uses non-local recovery for a
>   returned or raised language error. A handler may protect a procedure call
>   that suspends (S7.6.7v5), with the same single-evaluation, result, and
>   fresh-outcome rules after resume. [TE-16.1, TE-16.2, REH-D8, REH-D12]

> - **S7.6.3v2** **Postfix `e^` propagates** and occupies the same
>   left-associative postfix-primary tier as the handler, member access, and
>   query access. Its operand is a primary expression; parentheses admit a
>   wider expression. The propagation construct owns its caret — a call has no
>   optional trailing caret. Propagation yields the error-free success type (a
>   type-narrowing operator — `let b = a()^` gives `b : T`,
>   lane-eligible) and forwards the combined error set to the enclosing
>   function's declared channel. In a declared plain-`T` function it is a
>   compile error. `^` on an operand whose *explicit* type excludes error is a
>   compile error; where cleanness is merely inferred, a defensive `^` is
>   warn-only. [TE-13]

> - **S7.6.5v2** The retired forms `let a^err = e` and prefix `^err` /
>   `if (^err)` do not exist (why: the TE-13/TE-16 record); the test is spelled
>   `e is error`. `^` appears in four syntactic contexts — postfix propagation,
>   postfix braced-handler delimitation, the type-level channel, and the
>   handler-local current-error atom — all meaning "the error channel". There
>   is no general prefix error test or prefix braced-handler shorthand.* [TE-16]

> - **S7.6.6v4*** Division of labor: `or` defaults ordinary falsy values and
>   preserves errors;
>   `^ { }` catches errors only, with access and normal-value pass-through;
>   `match` branches explicitly between error and non-error outcomes;
>   `e^` catches errors only, propagating. Both caret forms remain
>   channel-agnostic; `or` acknowledges neither error channel. The handler has
>   no general-cleanup role. [TE-16.2, TE-22]

> - **S11.4.2** Signatures spell both failure dimensions: plain `T` excludes
>   null *and* error; absence is `T?`, failure is `T | error` (soft) or `T^E`
>   (enforcing). **Declared returns are effect firewalls**: a plain-`T`
>   function with an open body is a compile error — the author must *contain,
>   disclose, or impose*. An unannotated fn is implicitly contracted
>   `(any \ error, …) -> any \ error` and enforced identically; disclosure is
>   soft (`| error`), so a pushed-open function never spills `^` onto callers.
>   [TE-5, TE §10.7]

> - **S7.1.3v2*** Every invalid member/index **write raises a hard language
>   error** through the `T^` channel of S7.4.2 — never a soft `T | error` value,
>   null, or a silent no-op. This includes the same bounds, key-domain, and base
>   failures covered by S7.1.1v3. *Reads ask a question; writes issue a command,
>   and a command that silently does nothing hides bugs.* Growth is explicit
>   (`push`/`splice`). [C5, C5.3b]

> - **S12.3.4*** **`call(f, args)` is the dynamic-application form.** It
>   applies `f` to the members of the array `args` as individual arguments,
>   and is the sanctioned way to forward a collected argument list — notably
>   `fn outer(...) => call(inner, varg())`, which nothing else expresses when
>   `inner` is itself variadic. Its colour follows `f` (S12.1.4v3). Three
>   consequences follow from its being dynamic by construction, and are
>   accepted rather than worked around: arity is checked at run time, not
>   statically (S12.3.1 still bounds the callee's own slots); the result type
>   is `any`; and the call takes the dynamic ABI, not a direct-call fast path.
>   A `pn` target reached from `fn` context is an error — statically when `f`'s
>   colour is known, at run time otherwise. Error convention follows the
>   resolved colour: an `fn`-coloured `call` **returns** an error value, a
>   `pn`-coloured `call` **raises**. `args` must be an array; any other type is
>   an error.

> - **S12.1.4v3*** **Effect polymorphism: the `function` declaration.** A
>   function declared with the keyword `function` takes its colour from its
>   arguments rather than declaring one — *pure iff its function arguments are
>   pure*:
>
>   ```lambda
>   function apply_all(f: function, xs) => for (x in xs) f(x)
>   ```
>
>   (1) **Colour per call:** a call is `pn` if any argument bound to a
>   `function`-typed parameter is a `pn`, and `fn` otherwise; parameters typed
>   `fn (...)` or `pn (...)` keep their fixed colour and do not vote.
>   (2) **The body is checked as an `fn` body** (S12.1.2 applies, and no
>   statically-known `pn` may be called): its only effects are calls through
>   its polymorphic parameters — which is what makes (1) sound. (3) The colour
>   is resolved **statically** when every polymorphic argument's colour is
>   known and checked at **run time** otherwise; an `fn` caller passing a
>   statically-known `pn` is a compile error, and the error convention follows
>   the resolved colour as for `call` (S12.3.4). (4) A closure that escapes a
>   `function` capturing a polymorphic parameter is typed `function`, and
>   calling it is resolved under (3). (5) **A `function`'s own value is `fn`**
>   (S11.1.5): it adds no effect of its own, so it may cross an `fn (...)`
>   contract; soundness comes from (6). (6) **S12.1.1v2 holds for dynamic
>   callees:** in `fn` context a call through a value is checked at run time —
>   a `pn` callee is refused, and a `function` callee refuses a `pn` in a
>   polymorphic slot — returning the error value. `call(f, args)` is the
>   built-in instance with one polymorphic parameter. *Colour what changes the
>   caller's contract* — the keyword makes the dependence visible in the
>   declaration. [C20-2–C20-6, C20.6, Features §3.6]

> - **D5.2.1v3** Function wide-scalar returns use the **companion-lane
>   convention**: a may-be-wide boxed return is the pair `[item, scalar]` —
>   lane 2 a raw 64-bit payload (never a pointer), live iff lane 1 is a
>   **pending Item** (reserved tag `0x1E`, kind in the payload). A pending
>   Item never lives in memory and at most one is live at a time; the
>   caller resolves at first consume (free — the existing tag dispatch),
>   patches on escape (one branch), or forwards on tail return (free).
>   Typed returns use native lanes with `^E` on a second error-Item lane.
>   Classification happens at the wide value's *birth site*, statically —
>   never at boundaries. Lambda and LambdaJS generated functions have no
>   trailing caller-home parameter and no selectable v2 convention; their
>   C-reachable wrappers use `Context::mir_companion_slot`. A separately
>   versioned hosted-language ABI may still request an explicit payload-owner
>   address, but that is a native ownership boundary, not this return ABI.
>   (Reinstates SF14-v1 two-lane returns; retires v1's caller-donated canonical
>   homes — retired-ABI record in Appendix A.) [RV1–RV13, Return_Value §2–§5]

### Moved legacy error-channel records

~~Separate raised/returned categories determine caller obligations.~~
The following records are **retired historical material**, moved from Type
Enforcement on 2026-11-02. They preserve the original alternatives, batch vs
command rationale, implementation observations, and return-firewall reasoning.
TE-13.2 supersedes their channel split; other superseded syntax and control-flow
rules retain the historical status identified elsewhere in Appendix S.
Relative links in the quoted records remain relative to the `vibe/` directory.

#### Earlier three-channel taxonomy

> ### The three-channel taxonomy — the error model in one statement (2026-07-30, user)
>
> > **Lambda has exactly three failure channels. Two are types — always visible in the
> > signature, always enforced. The third is never a type — the system owns it.**
>
> | Channel | Spelling | Discipline | Designed for |
> |---|---|---|---|
> | **Value errors** | `T \| error` | soft — errors flow as data, no caller obligation; detected at type boundaries (`is`, `match`, annotated bindings, firewalls) | fn code: pipelines, batch, for-loops, containers |
> | **Raised errors** | `T^` / `T^E` | enforcing — engaged at the immediate expression (`^err`, `^`, `match`, `or`, error-admitting destination); `raise` licensed only here | pn code, I/O boundaries |
> | **System faults** | *(none — never in types)* | unchecked — stack exhaustion, out-of-memory, `==` depth-limit; transparent through fn frames; caught at a pn `^err` boundary or the global handler (C14) | the runtime |
>
> **Why the clean default is enforced at every fn boundary — the null parallel (user,
> 2026-07-30).** Lambda already makes *absence* explicit at signatures: plain `T` excludes
> `null`, and nullable must be spelled `T?` (TE-11, ships in P0). Error is as dangerous as null
> — arguably more: a stray null yields absence, while a mishandled error is a poisoned
> computation carrying a diagnosis nobody read. So error receives exactly the same treatment:
> plain `T` (and the implicit `any \ error` default) excludes `error`, and error-possibility
> must be spelled. **Null and error are the two failure dimensions of a signature — both
> explicit, both visible, both enforced:**
>
> ```lambda
> fn f() T             // present and valid — no null, no error; enforced
> fn f() T?            // may be absent
> fn f() T | error     // may have failed — soft, value-flowing
> fn f() T^            // may have failed — handling enforced at call sites
> ```
>
> ---

#### Earlier TE-13 two-form design and analysis

> ### TE-13 — Unified discharge surface over the two error forms (revised 2026-07-29, user)
>
> > **Historical record.** The pre-TE-16 analysis below retains the then-proposed `^err` and
> > prefix-test spellings. The landed surface is the TE-16 ruling below: braced handlers,
> > postfix propagation, and `is error`, per **S7.6.5v2**.
>
> **Two error forms, distinguished by obligation — different types, not one type.** (This
> revises the earlier draft's "one type, two provenances" identification.)
>
> 1. **`fn a() int^` — the enforcing/originating channel.** Raise-capable; callers **must**
>    handle (E228). The `^` spelling is **explicit-only — inference never produces it.**
> 2. **`fn b() int | error` — the non-enforcing union.** Error is simply one of the possible
>    values; it flows freely with no call-site obligation. **This is the form openness inference
>    produces**, and it is the form the formal semantics already assigns to system-fn value
>    channels (§7.3: "`T?` or `T | error`, never `T^E` on a system fn").
>
> In **value positions** (bindings, parameters), the two spellings are **equivalent** — decided
> 2026-07-29: `let x: int^ = a()` and `let x: int | error = a()` mean the same thing; the binding
> carries the outcome, value or error. `^` is semantically distinctive only as a *function
> return* marker, where it adds the enforcing obligation and the `raise` license.
>
> The provenance principle thereby becomes *syntactically manifest*: `^` is always a user-written
> explicit fact, enforced strictly; `| error` is the inferable fact that flows. **`raise` goes
> with `^` — DECIDED 2026-07-29 (user).** `raise` is licensed only by a declared `^`; in a
> `T | error` function, user code constructs `error(...)` and **returns** it as an ordinary
> value — `raise` there is a compile error (the existing plain-`T` raise restriction extends to
> the union form). Channel ↔ verb: `^` raises, `| error` returns — exactly §7.3's "fn return
> error; pn raise error" given surface spellings.
>
> **Initializer acknowledgement (revised 2026-11-02, USER).**
> **S7.5.1v3/S7.5.2v3** accept a binding whose declared or inferred receiving
> type admits the initializer's error outcome. `let x = a()` can retain the
> outcome without an annotation; `any` and `any | error` acknowledge identically.
> A known incompatibility is still a static error. If an explicit contract's
> error admission is undecidable, emit a warning and retain the ordinary runtime
> check. Unannotated inference conservatively retains the error possibility.
>
> The full decision, the static-error and warning alternatives, examples, and
> open follow-ups are now in
> [TE-13.1](Lambda_Design_Type_Error_Handling.md#te-131--initializer-acknowledgement-by-type-admission).
> This change concerns initializer bindings; the existing immediate-expression
> boundary and other receiving-position rules remain. `^ { }`, postfix `^`, and
> `match` with an error arm remain explicit acknowledgement forms. The superseded
> policy and its rationale have moved to
> [Appendix S](Lambda_Design_Type_Error_Handling.md#earlier-acknowledgement-policy-2026-07-29-superseded-for-initializers).
>
> **The soft form's contract (decided 2026-07-29, user): flow through the interior, detect at
> the type boundary.** Softness is *desirable* for fn code — pipelines, batch processing,
> for-loops, composed containers — because one returned error cascades like a normal value and
> **does not abort** the surrounding computation. The cost, *by design*: the error is likely
> embedded in the eventual result rather than surfacing upfront. The mitigation is the type
> system itself — typed containers exclude error by element type (§10.6), so a type pattern
> harvests embedded errors at whatever boundary the user chooses. All three legs verified live
> 2026-07-29:
>
> ```lambda
> [123, error("m"), 456]              // → [123, error, 456] — composition holds the error
> for (x in [1, "abc", 3]) int(x)     // → [1, error, 3]     — one bad element, batch continues
> type IA = int[]
> [1, error("m"), 3] is IA            // → false             — the type pattern detects it
> [1, 2, 3] is IA                     // → true
> ```
>
> So softness and enforcement are complementary halves, not a tension: errors flow freely
> through the interior, and are detected exactly where the user asserts a clean type — an `is`
> pattern, a `match` arm, an annotated (DEFERRED) binding, or the §10.7 firewall at a declared
> interface. *Side-finding (grammar-checked 2026-07-29):* the `is` RHS is plain **expression space** — `is`
> is a generic `binary_expr` table row with `operand = $._expr` (`grammar.js:76`, `:41-42`) —
> which is why its type forms are restricted by design (bare identifiers, base types, `[T]`
> literals; anything richer would collide with legal postfix parses: `x is int[3]` already
> parses as `(x is int)[3]`). Two in-grammar precedents show type-space RHS is workable where
> unambiguous: **`match` case patterns take the full `$._type_expr`** (`grammar.js:763`) —
> `case int[]:` works *today* and detects embedded errors (verified:
> `match(v){ case int[]: "clean" default: "has non-int" }` → `["clean", "has non-int"]`) — and
> the query operator takes `$.primary_type` (`:489`). **Noted only — OUT OF SCOPE for this
> proposal (user, 2026-07-29): supporting `x is (int[])` belongs to the pattern-grammar and
> validator design.** Hand-off note for that effort: the parenthesized form is strictly additive
> (`(int[])` is a syntax error everywhere today); mechanics would be a dedicated `is` rule with
> RHS `choice($._expr, seq('(', $._type_expr, ')'))`, one GLR conflict at the paren boundary
> (contents valid in both spaces, e.g. `(int)`) resolved by preferring the type parse — semantics
> coincide, since the AST already resolves identifier exprs to types. Direct unparenthesized
> `is int[]` should stay off the table (it would re-parse the currently-legal
> `(x is int)[…]` postfix form). Until then the spellings are: a named type, or a `match` arm.
> Same bucket, found 2026-07-30: the **`!` exclusion operator is broken for general types** —
> `type NE = any ! error` fails at pattern compilation (`compile_pattern_to_regex: unknown node
> type`, `invalid perl operator: (?!`): the exclusion is routed through the string-pattern regex
> compiler. Consequence: the non-error top `any \ error` (TE-5, `Lambda_Formal_Semantics.md`
> §7.3) currently has **no working surface spelling** — another item for the pattern-grammar
> and validator design (aligned with TE-5's "until the validator gains a distinct surface
> alias").
>
> **`or`-rescue — RESOLVED 2026-07-29: no rule-bend needed; error-consuming `or` is already both
> the spec and the implementation.** Specified three times over — truthiness (errors are falsy,
> `Lambda_Formal_Semantics.md` §3), §7.3 ("both are falsy, so `f(x) or default` rescues both
> uniformly"), and `Lambda_Error_Handling.md`'s falsy-errors section (`divide(10, x) or 0`) —
> and verified live 2026-07-29: `int("abc") or 0` → `0`, `error("boom") or 5` → `5`,
> `f(0) or 5` → `5` (user-fn div-by-zero — *historical: pre-C14c; `div 0` now yields `inf`,
> which is truthy, so the `or`-rescue no longer applies to division*), `if (int("abc"))` →
> falsy. The coherence argument:
> `or` *is* truthy-select, and errors are falsy, so consumption follows from the definitions —
> error-propagating `or` would require either error-truthy (absurd: `if` would enter) or a
> special-cased `or` that breaks the identity. Two safety properties observed: **`0` is truthy**
> in Lambda, so `int(s) or 0` has no JS-style zero-swallow footgun; and errors log at
> origination (`runtime error [318]: boom`), so an `or`-consumed diagnostic leaves a `log.txt`
> breadcrumb even though the program never sees it. `a() or 0` accordingly counts as E228
> engagement (textually explicit consumption handling both branches). Pitfall documented
> 2026-07-29 in `Lambda_Error_Handling.md` §"Error Truthiness": bare `error` in expression
> position is the **type** (truthy, not an error value) — `error or 0` → the type,
> `error is error` → `false`, `error is type` → `true`; the `if`-condition lint catches the
> `if (error)` form but not the `or`-operand form (lint-extension candidate).
>
> **The `or`-typing rule (required by P0).** For the idiom to survive strict declarations, the
> static type of `a or b` must narrow the falsy poison/absence members out of the left side:
> `type(a or b) = (type(a) − {error, null}) | type(b)` — plain union arithmetic, no flow
> analysis. Then `let n: int = int(s) or 0` types as `int` and passes the P0 checker; without
> this rule the new strict declarations would reject the spec-blessed idiom.
>
> **Operators are value-directed, not declaration-directed.** All the existing forms operate on
> the error-ness of the value, so they work identically over both error forms:
>
> | Form | Semantics (unchanged) | On `T \| error` (incl. inferred) |
> |---|---|---|
> | `let x^err = e` | on error: `x = null`, `err` = the error; else `err = null` | identical — the discharge point |
> | `e^` (postfix) | unwrap success or return the error from the enclosing function | identical — makes the enclosing fn open (or rides its declared channel) |
> | `^e` / `e is error` | boolean test | identical |
> | `e or default` | errors are **falsy** → default idiom | identical — the batch one-liner: `a(xs[i]) or 0` |
> | `raise` | requires a *declared* `T^` | **invalid** — a `T \| error` fn *returns* `error(...)` as a value; only `^` licenses `raise` |
>
> Emission differs invisibly by callee kind — can_raise dual-lane reads the side channel, boxed
> open results tag-test the Item — but the surface must never reveal which.
>
> **Destructuring across mixed channels (decided 2026-07-29, user).** `^err` is **total over
> error-ness and channel-agnostic**: it splits the outcome by whether the *value* is an error,
> regardless of which channel delivered it — the enforcing `^E` channel, an error type inside
> the value union, or an error hiding inside `any`. The two boundary cases that pin the rule:
>
> ```lambda
> fn a() T | e1 ^ e2      // legal syntax (nobody would write it, but allowed):
>                         //   union error e1 AND channel error e2
> let b^err = a()         // b : T          — every error constituent stripped
>                         // err : e1 | e2  — errors from BOTH channels land in err
>
> fn c() any ^ e2
> let b^err = c()         // b : any \ error, guaranteed non-error
>                         // err : error | e2 — errors arriving via the any value or the channel
> ```
>
> The general typing rule: `type(b)` = the success constituents of the source type (`T` in the
> first case; `any \ error` in the second); `type(err)` = the source's error constituents ∪ the
> channel's error type (∪ `null` on success). Note `any \ error` is exactly what the validator's
> `any` already means — it matches everything except ERROR (§5.3) — so the checker's refinement
> and validation positions agree. At runtime the split is one channel-agnostic test: `is error`
> on the outcome (dual-lane callees: side-lane check plus value-tag test; boxed callees: tag
> test alone). E228 applies whenever the signature carries any `^` channel, and the destructure
> engages *everything* at once. **Postfix `^` behaves identically on the same mixed forms —
> CONFIRMED 2026-07-29 (user):** it unwraps to the same stripped success type (`T`, or
> `any \ error`) and propagates the same combined error set (`e1 | e2`), which rides the
> enclosing function's error channel per the unified-channel rule. In binding position the
> stripping is manifest in the type — `b` is error-free in both forms:
>
> ```lambda
> let b^err = a()   // b : T (stripped); err : e1 | e2
> let b = a()^      // b : T (stripped) — same type; errors propagated instead of captured
> ```
>
> Postfix `^` is thus a *type-narrowing* operator in expression position: downstream of either
> form, `b` is statically clean and eligible for native lanes — the two forms differ only in
> where the error goes (captured locally vs propagated to the caller).
>
> **Obligations attach to the form.** Must-handle (E228) applies to `T^`/`T^E` — the enforcing
> spelling — and to pn/can_raise sys funcs; `T | error` never triggers it (the no-spill
> decision). This is the simplest possible keying: no provenance metadata needed, the type
> spelling *is* the obligation. Doc consequences for `Lambda_Error_Handling.md`: the `T` row of
> the return-type table ("always succeeds — no error possible") holds for clean functions; an
> open *expression or binding* is effectively `T | error`; every fn return — declared plain-`T`
> or the implicit `any \ error` contract of an unannotated fn (2026-07-30 restatement) — is a
> firewall, so an error-possible body is a compile error per §10.7; and "raise is the **only** way to
> return an error from a function" becomes `^`-channel-specific — the `| error` form returns
> `error(...)` values directly.
>
> **Explicit vs implicit error-ness (decided 2026-07-29).** Type inference tracks *where* the
> error-possibility came from, and enforcement keys on that provenance:
>
> ```lambda
> fn f(a: T^) { let b: T = a }   // compile error — explicit T^ must be discharged first
> fn g(a)     { let b: T = a }   // DEFERRED: success binds b:T; failure exits before b exists
> ```
>
> An *explicitly declared* error possibility — either spelling, `T^` or `T | error` — must be
> visibly engaged before the value can enter a plain-`T` position: `let b: T = a` is a compile
> error for both. **When the user is explicit, we check explicitly.** Only *implicitly*-open
> values (inferred openness, `any`) cross plain-`T` boundaries as DEFERRED checks. The complete
> binding rule for `x = a()` where `a` is declared `int | error` (decided 2026-07-29):
>
> ```lambda
> let x: int = a()            // compile error — explicit claim contradicts a's declared type
> let x: int | error = a()    // legal — x carries the outcome, value or error
> let x: int^ = a()           // same as the line above — equivalent in value positions
> let x = a()                 // silent — x infers int | error, takes whatever a() returns
> ```
>
> This is the same provenance split as the vacuous-`^` rule below — explicit facts are enforced
> strictly, inferred facts flow. Consequence worth naming: `| error`-declared sys funcs (§7.3's
> `int(s)` family) reject plain-typed bindings too — `let n: int = int(s)` errors; the idioms
> are `int(s) or 0`, `^err` destructuring, or the union binding.
>
> **Propagation into a declared channel — DECIDED 2026-07-29 (user).** System and user-declared
> errors ride the **same** channel: every `R^E` is operationally `R^(E | error)`. `E` constrains
> *user-raised* errors; defects flow implicitly. Caller consequence, to be documented: after
> `let a^err = g()`, `err` may hold a *system* error (e.g. a type defect), not only the declared
> `E` — code matching on `err.code` or shape must not assume `E`.
>
> **Vacuous discharge — DECIDED 2026-07-29 (user): split by the provenance of the cleanness.**
> `e^` where `e` is provably non-error differentiates two cases: (1) the operand's
> error-possibility was in play and its cleanness is an *inference* result (an open-capable
> callee currently proven clean) → **warn only, or even silent** — a defensive `^` survives
> distant clean↔open flips; (2) the non-error nature is *explicit* (declared plain-`T`, no open
> capability in play) → **static error**, as today ("'add' does not return errors"). *(Reading
> of case 2 confirmed 2026-07-29.)*
>
> **Interaction with TE-11:** after a failed `let x^err = e`, `x` holds `null` (current spec), so
> the value binding is effectively `T?` until `err` is checked. Flow narrowing stays KIV; the
> discipline is documented, not enforced.
>
> **Short-circuit refinement (for TE-8/TE-9):** an error value short-circuits a checked boundary
> only where the target type does not admit it. Targets `T^`, `T | error`, `any`, and `error`
> receive the error *as a value* — an error-typed parameter is the explicit opt-in to
> error-transparency (spelled `T | error` for non-enforcing acceptance).
>
> **Implementation notes.** (1) The live propagation operator is postfix `^` (verified
> 2026-07-29: `input(...)^` works; `input(...)?` parses as the *query* operator and does not
> propagate). The two E228 diagnostic texts advertising `d(...)?` are **fixed** (2026-07-29,
> `build_ast.cpp:5199`, `:9017` → `d(...)^`); the error-handling guide is updated with TE-13's
> channel-agnostic discharge rules. (2) The batch
> idiom for per-item failures is simply a **plain array** holding successes and errors (§10.6's
> type-level framing); typed `int[]` stays clean-only, and no new `(int^)[]` spelling is needed.

#### Earlier TE-16 three-regime acknowledgement table

> #### Acknowledgement taxonomy for the enforcing channel
>
> **S7.5.1v3/S7.5.2v3** define acknowledgement at the immediate expression:
> a handler, postfix propagation, `match` with an error arm, an initializer
> binding under the admission rule, or a declared parameter/return under the
> existing explicit-error contract rule. The initializer decision and warning
> policy are recorded in
> [TE-13.1](Lambda_Design_Type_Error_Handling.md#te-131--initializer-acknowledgement-by-type-admission).
>
> An undecidable explicit initializer contract warns and defers admission.
> Under **S7.7.2v2/S11.4.1v4**, the admitted value or validation error is bound,
> and subsequent statements execute. The effective type retains the error
> possibility; there is no declaration-driven block exit. Other checked
> boundaries retain their own contracts.
>
> | Class | Mechanism | Compile-time obligation |
> |---|---|---|
> | Soft error values (`T \| error`) | Flow as data | Ordinary type contracts |
> | Defects (failed boundary checks) | Containment at the applicable boundary | Existing boundary rules |
> | Raised errors (`T^E`) | Immediate acknowledgement | Initializer admission per S7.5.2v3; other unacknowledged calls retain E228 |

#### Earlier open-question resolution: two error forms

> 2. **One discharge surface for two error channels — elaborated as TE-13, revised 2026-07-29
>    to the two-form model.** `T^` = enforcing (E228, raise-capable, explicit-only); `T | error`
>    = non-enforcing union (what inference produces). Discharge forms (`^err`, postfix `^`,
>    `^e`/`is error`, `or`-defaults) are value-directed and work identically over both.
>    Residuals (a)/(b)/(c) all **DECIDED**: the obligation split is *type-directed* — attached
>    to the `^` spelling itself, closing (a); `R^(E | error)` widening per the unified channel;
>    vacuous-`^` warn/silent when cleanness is inferred, static error when explicit.
>    **CONFIRMED 2026-07-30:** `let x: int | error = a()` counts as immediate E228 engagement.

#### Earlier return-firewall contain/disclose/impose rationale

> 7. **Clean-ness inference mechanics — DECIDED 2026-07-29 (user, incl. the division
>    consequence): declared return types are effect firewalls; bindings are not (§10.8).**
>    The rule: a function with an explicitly declared plain-`T` return whose body is open
>    (calls an open function, or contains an error-originating operation whose result reaches
>    the return) is a **compile error** — the author picks from a three-way menu: **contain**
>    the openness locally (`^err` + handling, `or` default), **disclose** it as `T | error`
>    (non-enforcing — callers see it, owe nothing), or **impose** it as `T^` (enforcing —
>    callers must handle). Inference itself only ever produces `| error`, never `^` (TE-13
>    two-form model). `-> any` is the explicit error-admitting escape (TE-5). **REVISED
>    2026-07-30 (user restatement): undeclared returns are no longer silent** — an unannotated
>    fn is implicitly contracted `(any \ error, …) -> any \ error` and enforced exactly like a
>    declaration: inference finding an error-possible return is a reported type error. The
>    firewall thus covers *every* function; the default contract is `any \ error`. Consequences:
>    - **§10.7 mostly dissolves.** Silent clean→open cascades stop at the first declared frame
>      (stability); recursion needs no fixpoint at all — every fn now has a contract, declared
>      or the implicit `any \ error`, so assume it and check the body; `pub` fns need no effect
>      metadata (recommend, not require, declared returns on `pub`). A function *value* invoked
>      dynamically remains per-call-site open, as before.
>    - **Fixes an inversion**: explicit `open_call()^` inside a plain-`T` fn is *already* a
>      compile error (`Lambda_Error_Handling.md`); implicit propagation slipping through would
>      have been backwards. Zig (`catch` forced in non-`!T` fns), Swift (`do-catch` in
>      non-throwing), Rust agree.
>    - **Emitter consequence**: declared-plain-`T` function bodies are clean-return by
>      construction. A later native variant may exploit that fact; TE-14 deliberately leaves the
>      carrier/ABI choice out of the semantic phase.
>    - **No E228 spill**: inference never produces `^` at all — it produces `| error`, which
>      never triggers must-handle. A function pushed open by its arithmetic discloses as
>      `T | error` with zero caller impact; `^` is reserved for authors *choosing* to impose
>      handling. Must-handle stays exactly where it is today (declared `^` and pn/I-O raisers,
>      §7.3).
>    - **Interior vs interface (settles §10.8's residual)**: unannotated bindings flow —
>      `let x = open_call()` infers `T | error`. Annotated bindings are contracts —
>      `let x: int = dynamic_call()` checks at the boundary, establishes `x: int` on success,
>      and produces an error before `x` exists on failure. Declared returns remain effect
>      firewalls.
>    - **Consequence, REVISED by C14c (2026-07-30)**: division no longer error-originates at
>      all — `/`, `div`, and `%` stay in number (`int div int → float`, IEEE `inf`/`nan` on a
>      computed zero; `Lambda_Formal_Semantics.md` §4.7). The firewall's error-originating class
>      is therefore exactly the set of `| error`/`^`-declared calls; pure-math bodies are clean
>      by construction. (`fn avg(a: int, b: int) int { a div b }` still fails — but as a plain
>      E208 return mismatch, body float vs declared int; the `float`-returning form passes.)
>      Joins the D2I sweep as a pre-P0 audit, and the diagnostic must state *why* the body is
>      open (first cause, e.g. "call to 'g' may return error"), making the "why open?"
>      diagnostic part of the error UX rather than a nice-to-have.
>    - TE-9's open-case wording is revised accordingly: the silent effective-`T | error`
>      applies to *expressions and bindings* only; every fn return — declared, or implicitly
>      `any \ error` — enforces (2026-07-30 restatement).
>    - **Recursion and system faults — DECIDED 2026-07-30 (reaffirming C14, 2026-07-06): we
>      handle it for the user.** `fn f() T` with recursive `f` does *not* force
>      `T | recur_error`: infinite recursion / stack exhaustion is a **system/resource fault**,
>      and per the C14 semantics ruling such faults are the **unchecked third channel** —
>      raisable from anywhere including `fn`, invisible to fn types, transparent through frames,
>      caught at a pn `^err` boundary or the global handler (Java `Error`-class precedent; the
>      `==` depth-limit raise is the same class; the runtime hook `lambda_stack_overflow_error`
>      already exists). Forcing a declared `recur_error` would be checked-exception spill for a
>      fault no local caller can meaningfully handle. This also resolves a flagged tension:
>      C14's "`T | error ^ E` must never exist" is *system-channel-specific* — the fault channel
>      never appears in types — while TE-13's user-declared mixed form `T | e1 ^ e2` remains
>      legal. *Implementation note (added 2026-07-30): the next implementation phase must handle
>      the fault channel explicitly — stack exhaustion **and out-of-memory** (and the `==`
>      depth-limit raise) route as unchecked faults: transparent unwind through fn frames,
>      catchable at pn `^err` or the global handler, never typed error values. Detail that
>      matters: OOM cannot allocate its own diagnostic, so fault objects must be
>      pre-reserved/static — the §10.4 rich-error contract applies to the *checked* channels
>      only.*

### Additional retired channel wording

~~Validation, builtin, and logical-composition outcomes use separate soft and
raised obligations.~~ Replaced by TE-13.2 and S7.4.1v2/S7.5.1v4–S7.5.5.
These excerpts preserve the earlier alternatives and consequences. In
particular, the older concern about forcing recovery at every call is now
addressed by inferred retention and acknowledgement through composition;
batch-friendly flow no longer needs an exemption for soft errors.

> ### TE-9 — Failed checks produce error **values** (decided 2026-07-29; supersedes rev 1's panic recommendation)
>
> Three options existed; two are rejected by decision:
>
> 1. **No panic mode.** Lambda's semantics must stay friendly to batch processing — one bad
>    record in ten thousand must not kill the run. A type failure is a per-item outcome, not a
>    process-fatal event.
> 2. **No forced `T^E`.** `T^E` is the *user's* declared channel; conscripting it for implicit
>    checks would virally impose `^err`/E228 handling on every caller — error checking spilled
>    all over Lambda code.
> 3. **Therefore: error return values.** A failed DEFERRED check yields a diagnostic-carrying
>    error value in the value lane — Lambda's existing errors-as-values model (and the Jube
>    C-ABI principle: errors as return values everywhere). At an annotated destination, that
>    error is the boundary operation's result; it is not stored in the declared `T`.
>
> **Annotated and inferred outcomes stay distinct.**
>
> - `let x: T = e` checks `e`; success establishes `x: T`, while failure produces the error
>   outcome before `x` exists.
> - `let x = e` establishes `x` with `e`'s inferred effective type, including `T | error`.
> - A statically proven call to `fn a(b: int) int` has result `int`. A dynamically checked call
>   is open at the call boundary because parameter validation can fail, so that call expression
>   has effective type `int | error`; on success the callee body still sees `b: int`.
> - Every fn return is an effect firewall — a declared plain-`T`, or the implicit `any \ error`
>   contract of an unannotated fn (2026-07-30 restatement): an error-possible body is a compile
>   error, resolved by containing, disclosing `T | error`, or imposing `T^`.
>
> Boxed storage is therefore required for inferred/open outcomes, not for a successfully
> established annotated `T`. A future native variant may exploit the clean proof, but the semantic
> rule does not depend on that optimization.
>
> **Error short-circuit rule.** An error value arriving at any subsequent DEFERRED boundary
> becomes that boundary's result without further checking — "error in, error out", the
> `GUARD_ERROR1` convention generalized. Propagation is therefore *implicit*; containment is
> *explicit* and placed where the user chooses (`let x^err = …`, postfix `^`, `or`,
> `x is error`). This is what keeps the no-spill promise: nothing forces handling for an inferred
> `T | error`, and errors surface at the batch boundary where the user aggregates results.
>
> **Sys functions.** The normative convention is now the two-way admission policy in
> `Lambda_Design_Sys_Func.md`: each function classifies its invalid type/value cases as either
> **admissive** (return the documented result-domain absence, such as `null` for `argmin([])`)
> or **non-admissive** (return a detailed `error`, or raise through `T^E`). Neither branch may
> silently produce a wrong value or a message-less death. A future perf split mirrors this
> contract: per-function clean/open versions (`len(non_error_data) int` vs
> `len(any) int | error`) are selected only after the caller's static argument classification,
> while admissive result types remain visible in the public signature.
>
> Open results travel boxed, while a backend may use native lanes where cleanness and carrier
> proofs establish that no error can occupy the lane. The exact entry/return ABI is deferred to
> the implementation/performance stage (TE-14).
>
> For validation failures at named-type boundaries (TE-10), the error value carries the
> validator's path detail (`.field[3]: expected int, got string`) — which requires surfacing
> `ValidationError` detail that today dies in `log_debug` (§5.3).

> When absence would hide a malformed source, unreadable resource, or violated
> operation contract, the system function is **non-admissive** and returns an
> error value or raises through `T^E`. `parse` on malformed input and `input` on
> a failed load are examples.

> | Non-admissive system-function operation | Detailed `error`, or raised `T^E` for effectful operations |

> - **Channel-agnostic**, per TE-13: `^ { }` discharges both raised (`T^E`) and soft
>   (`T | error`) errors.

>   loss, since faults are not resumable. This keeps the three regimes distinct: soft values flow
>   as data, defects are gated by TE-17 before they can reach a lane, raised errors stay E228-gated.

> **Acknowledgment and types.** `or` no longer acknowledges a raised `T^E`
> channel, and its type retains any reachable error alternative. A bare
> `let x = hard_call() or fallback` is no longer an acknowledgment. Use
> `hard_call() ^ { fallback }` to recover, or `hard_call()^ or fallback` to
> propagate and default a successful falsy value. Explicit error-admitting
> destinations and `match` error arms remain valid under **S7.5.1v2**.

>    principle: system `fn` failures are values, channel `T?` or `T | error` (never `T^E` on a
>    system fn), with admissive no-answer cases using `null`/`[]`/`""` and non-admissive cases
>    using detailed `error()`. §7.3 also confirms `input`/`fetch` as pn-family raisers (E228
>    conformant) and supplies the wrapper idiom for set-oriented input.

### Handler convenience before filtered recovery (superseded 2026-11-02)

~~Only the unfiltered `e ^ { h }` handler is available; handler-local `^`
always denotes an error, and `{` immediately after `^` is the sole handler
discriminator.~~ Extended by **S7.6.8–S7.6.9 / TE-16.3–TE-16.4**.

The single-handler decision removed a redundant success arm and its special
`~` context. It did not require recovery to remain all-errors-only forever.
Filtered handlers select errors while retaining the same value flow and body
rules; `or^` combines two common fallback conditions without making ordinary
`or` consume errors. Both retain one body and introduce no success/finally arm.
The old rules below are preserved for their exact former scope. In particular,
the caret subject is still error-only in error handlers; the broader subject
belongs specifically to `or^`.

> - **S7.5.1v4*** **Acknowledgement follows possible ordinary-error outcomes.**
>   A receiving context acknowledges through an `error` arm in `match`, the
>   `^ { }` handler, postfix `^`, an initializer under S7.5.2v3, or an
>   error-admitting parameter, return, or container destination under its
>   existing contract. Error origin and equivalent type spellings do not alter
>   the obligation. *Retaining or forwarding an outcome acknowledges it;
>   inspection, recovery, and proof of success are not mandatory.* Composition
>   follows S7.5.4; neither an unrelated binding nor an enclosing return
>   annotation acknowledges all expressions in its scope. [TE-13.1–TE-13.2]

> - **S7.6.1v7*** **`e ^ { … ^ … }` is the single postfix error handler and is
>   origin-independent** — it receives ordinary error values and (at `pn`
>   boundaries) system faults, binding the error to handler-local `^`
>   (innermost-wins). The operand is evaluated exactly once; the handler
>   introduces no `~` binding: `~` retains the current-value meaning supplied by an enclosing pipe,
>   match, constraint, or view context, and a non-error operand passes through
>   unchanged, including `null`, `false`, and `""`. Typing removes the handled
>   error alternatives:
>   `type(e ^ { h }) = (type(e) \ error) | type(h)`.
>
>   An error selects the handler body; its normal result becomes the expression's
>   result. An error produced by the body is a fresh outcome and is not consumed
>   again by the same handler. **The handler produces its normal result for both
>   `fn` and `pn` operands**, including when used as a standalone statement. In a
>   procedural sequence that result replaces the preceding result under
>   S12.1.2v2; a later result may replace it in turn. A final handler supplies
>   the block's result. A `null` result replaces an earlier value; it is not
>   suppressed. `return` inside the body exits the enclosing procedure, not just
>   the handler; `raise`, `break`, and `continue` retain their ordinary targets.
>
>   The receiving type must retain any error returned by the handler body;
>   using a handler does not by itself prove a clean result. A body that exits
>   through `raise` / `return` contributes no normal recovery result. The
>   outcome type and its receiver obey S7.5.4 and SI14v2.
>
>   Ordinary error outcomes always reach this handler by normal return and a
>   local generated branch; the handler never uses non-local recovery for a
>   ordinary language error. A handler may protect a procedure call
>   that suspends (S7.6.7v5), with the same single-evaluation, result, and
>   fresh-outcome rules after resume. [TE-16.1, TE-16.2, REH-D8, REH-D12]

> - **S7.6.2v4*** The handler is the left-associative postfix-primary form
>   `primary ^ { error_body }`. It occupies the same precedence tier as member
>   (`.`) and query (`?`) access. Consequently `a + b ^ { h }` means
>   `a + (b ^ { h })`; handling the complete binary expression requires
>   `(a + b) ^ { h }`. The result is primary-like, so postfix operations
>   continue left-to-right: `e ^ { h }.field` means `(e ^ { h }).field`, and
>   handler chains associate from the left. The body is brace-delimited.
>   **There is no success or cleanup suffix.** The former
>   `e ^ { h } ~ { v }` is a syntax error; it is not reinterpreted as `finally`
>   or unrelated trailing expressions. Use `match` for explicit outcome
>   branching. `^` followed by anything
>   other than `{` is propagation (`f()^ - 1` propagates then subtracts). There
>   is no prefix `^ { h } e` shorthand or alternative spelling for a second
>   arm. The caret belongs to the handler or propagation construct, never to
>   `call_expr`. [TE-16.2]

> - **S7.6.5v3*** The retired forms `let a^err = e` and prefix `^err` /
>   `if (^err)` do not exist (why: the TE-13/TE-16 record); the test is spelled
>   `e is error`. `^` appears in four syntactic contexts — postfix propagation,
>   postfix braced-handler delimitation, the type-level outcome shorthand, and the
>   handler-local current-error atom — all referring to errors. There
>   is no general prefix error test or prefix braced-handler shorthand.* [TE-16]

> - **S7.6.6v5*** Division of labor: `or` defaults ordinary falsy values and
>   preserves errors;
>   `^ { }` catches errors only, with access and normal-value pass-through;
>   `match` branches explicitly between error and non-error outcomes;
>   `e^` catches errors only, propagating. Both caret forms remain
>   origin-independent; `or` does not itself acknowledge errors. The handler has
>   no general-cleanup role. [TE-16.2, TE-22]

> - **S10.1.7v3*** **A body about one subject may leave `~` implicit; the pipe
>   family always spells it.** A body that binds `~` to one subject, its
>   current item, may read a field of it by the field's bare name, the OOP
>   receiver convention. Such bodies are a method (S8.2.3), an object-level
>   `that` constraint and a type constraint `T that cond` in any position
>   (S11), the `that` proviso (S10.1.5v3), and a `match` arm (S11.2.1).
>   An error handler binds `^` only and retains its enclosing `~` context
>   (S7.6.1v7); there is no handler success-arm context. [TE-16.2]
>
>   So `user that age >= 18` and `match user { case map: age >= 18 }` both
>   read `~.age`, and `~` may still be spelled. The current item is scoped to
>   its body: a nested body binds its own, spelled or implicit, and when it
>   ends the outer binding is back. In
>   `order that (match kind { case map: total }) + total == 9`, the first
>   `total` is `kind`'s and the second is the order's.
>
>   Declared fields (a method, an object constraint) are names of the body's
>   scope and shadow outer bindings. Elsewhere the current item has no
>   declared shape, so a bare name is the member `~.name` (S12.3.3v2) only
>   when no binding claims it: not a scope name, an `import math` constant, or
>   a module or namespace prefix (`m.sqrt(x)`). An implicit read is a read
>   only. It never supplies a callee — `xs that len(~) > 2` calls `len`, not
>   `~.len` — and never a write target: `x = 1` in an arm assigns the
>   variable `x` or fails, and never stores into the matched value.
>
>   A pipe-family body (`|>`, `|:`) reads bare names as ordinary names, even
>   nested in one of these bodies. `|>` tells mapping from application by the
>   free `~` its text spells (S10.1.2v4), and `|:` must read that text the
>   same way (S10.1.6). A field supplied behind the text would also let a
>   misspelled `xs |: is_evne` pass E238 as a silent `[]`. For the same
>   reason, a `~` that a nested body binds, spelled or implicit, is never free
>   in an enclosing pipe body (S10.1.3): `xs |> match (1) { case int: ~ * 10 }`
>   is whole-value application, not a mapping. [Expr_Pipe §F.7]

> - **S16.2.2v2*** After a **complete** expression, a line-start token that can
>   only continue an expression does continue it: `|> | & ! ? .? ** ++ % > =`,
>   `== != <= >=`, and the word operators `and or to in is at div that eq ne
>   lt le ge gt`, plus `else case default`. These are unambiguous in any
>   position and need no separator. `!` is here, not below: it is a pure
>   infix token (S16.8.1). [Design_Syntax §3.3, §7.1]

> - **S16.2.6*** A handler's brace opens **on the same line as its `^`**
>   (`expr ^ { ... }`). A trailing `^` followed by a line-start `{` is the
>   propagate-versus-handler ambiguity, and the same-line requirement is what
>   resolves it. [Design_Syntax §3.6]

> - **S16.4.3v2*** **Declaration braces are structural and never read as maps.**
>   `fn`, `pn`, `view`, and `on` bodies, braced match arms (`case T { ... }`),
>   error-handler bodies (`^ { ... }`), and `while` bodies are always
>   blocks; `{}` in each is the empty body. `while` belongs here by the same
>   value-use principle — it is procedural-only, so its body value is always
>   discarded and a map body is dead by construction. Each declaration has an
>   expression escape where a value body is wanted (`fn f() => {a: 1}`,
>   `case int: {a: 1}`). `match`'s outer braces delimit the arm list
>   (S16.6.4); `type` bodies have their own interior; map-*type* patterns
>   (`{a: int}`) live in type space and are untouched. The dividing test
>   between S16.4.1v4 and this ruling: interior decides exactly where a
>   bare-expression second form exists; the brace is structural exactly
>   where it is mandatory — handler bodies have no unbraced form, since
>   `expr ^ expr` is already propagate-then-continue (S16.2.6).
>   [Design_Syntax §5.9, TE-16.2]
>
>   | Braces | Reading |
>   |---|---|
>   | value/expression position, call args, initializers (`fn` **and** `pn`) | map or block by interior; `{}` = map |
>   | `if`/`for` bodies (both spellings), `else`, colon-form match arms, arrow bodies | map or block by interior; `{}` by fn/pn context (arrows: always fn) |
>   | `fn` `pn` `view` `on` bodies, braced match arms, error-handler bodies, `while` bodies | always block; `{}` = empty body |
>   | `match` outer braces | arm list (structural) |
>   | `type` bodies | fields/constraint/methods interior |
>   | content position | map item (`{}` = empty map item, meaningful) |
>   | bare statement position in `pn` | **error** (dead either way) |
>   | type-annotation `{...}` | type space, unaffected |

### `empty` keyword proposal (closed 2026-11-02)

~~Whether to reserve `empty` for `error | null` remains open.~~ Rejected by
**S7.5.7 / TE-16.3**: the need is not pressing, the name suggests other empty
values, and `type empty = error | null;` already provides a scoped user-defined
shorthand. The earlier discussion is retained below as history.

> ### `empty` keyword discussion — open
>
> The user asked whether `empty` should name `error | null`. **No keyword or
> predefined alias is ratified.** The current recommendation is to keep the
> explicit union or use a domain-specific ordinary type alias.
>
> The proposed name suggests empty strings and collections, but `""`, `[]`,
> and `{}` do not belong to `error | null`. An error also carries a diagnostic;
> it is not merely absent content. Naming the union `empty` would introduce a
> second meaning of emptiness alongside those ordinary values. It must also
> not be confused with no-result statements, the bottom type `none`, or the
> falsy set (which includes false and empty text but excludes errors).
>
> If a shared alias later proves useful, settle its name and whether it needs
> to be a builtin type name rather than reserving a keyword. This discussion
> does not block the two accepted handler forms or the existing union test.


### Earlier reassignment retention and exit rules (retired 2026-11-02)

~~A failed whole-variable reassignment keeps the previous value, exits its
owning block, and diagnoses the skipped tail/retained value; inferred initializer
acknowledgement does not extend to reassignment.~~ Replaced by **TE-18.2 /
S7.5.2v4 / S7.7.4v2**: the initializer admission rules apply unchanged,
including static rejection and deferred warnings. An admitted error or deferred
validation error replaces the old variable value, and execution continues.

The old rule protected a clean declared carrier by preventing later code from
observing a failed replacement, but it also prevented ordinary recovery after
that assignment and required tail-sensitive diagnostics. Continued outcome
storage removes that special flow. The target stays strict while effective
types track possible errors across assignments and joins. Field/element store
contracts are preserved separately; their redesign is not implied by this change.
The old module/closure failure clauses do not grant permission to mutate state
forbidden by S9.1.4.

The full earlier TE-18 mutation deliberation remains in the
[Type Enforcement history](Lambda_Design_Type_Enforcement.md#appendix-s--superseded-declaration-skipping-design).
The exact formal rules revised by this extension follow, including the
initializer-only scope and retained-value representation assumptions. They are
historical wording, not alternate current rules.

#### Prior SI14v2

> - **SI14v2 — A binding's static type is never a lie.** A readable binding's
>   effective type covers its actual outcome. A deferred initializer check
>   against declared `T` binds an admitted value or an error, so its effective
>   type retains the error alternative (S7.7.2v2); the annotation alone never
>   proves that outcome error-free. Failed reassignment retains its previous
>   value under its own contract. There is never a null-for-failure
>   placeholder. Every error value is deliberate; discharge strips error
>   constituents from its success type; no boundary silently substitutes
>   `0`, `null`, or reinterpreted bits. (Fault *timing* is exempt from SI3v2.)
>   [S7.4, S7.7, S11.4]

#### Prior S7.5.1v5

> - **S7.5.1v5*** **Acknowledgement follows possible ordinary-error outcomes.**
>   A receiving context acknowledges through an `error` arm in `match`, a
>   handler (S7.6), explicit error classification (S7.5.6), postfix `^`, an
>   initializer under S7.5.2v3, or an error-admitting parameter, return, or
>   container destination under its existing contract. A filtered handler
>   acknowledges only its matched error alternatives; residual errors still
>   need a receiver. Error origin and equivalent type spellings do not alter
>   the obligation. *Retaining or forwarding an outcome acknowledges it;
>   inspection, recovery, and proof of success are not mandatory.* Composition
>   follows S7.5.4; no binding or return annotation acknowledges unrelated
>   expressions throughout its scope. [TE-13.1–TE-13.2, TE-16.3–TE-16.4]

#### Prior S7.5.2v3

> - **S7.5.2v3*** **Initializer acknowledgement follows type admission.** A
>   `let`/`var` initializer is acknowledged when its declared or inferred
>   receiving type admits the possible error outcomes. `any` includes error
>   (S11.4.3), so `any` and `any | error` acknowledge identically; aliases and
>   equivalent type expressions do not change admission. An inferred binding
>   preserves the error possibility, using a conservative type if necessary.
>   A proven incompatibility is a static error. If an explicit contract's error
>   admission is undecidable, **warn and defer to the ordinary runtime check**
>   (S11.4.1v4); bind the admitted value or validation error and continue
>   (S7.7.2v2). The effective binding type retains the failure possibility.
>   Mere inference imprecision does not require an acknowledgement warning.
>   An admitted error is retained as a
>   value, so subsequent statements can execute. The warning permits unresolved
>   initializer admission; it never authorizes an unchecked value as `T` or an
>   implicit block exit. Other receiving-position rules are unchanged.
>   [TE-13.1, TE-18.1]

#### Prior S7.7.1v2

> - **S7.7.1v2*** **Initializer validation is an expression-level operation**
>   yielding an admitted value or a rich error. Failure of a `let`/`var`
>   initializer check never implicitly exits its declaring block. Validation
>   composes with ordinary handlers and explicit propagation; its standalone
>   surface spelling remains to be settled. In ordinary expression composition
>   (`a + e + b`) the error flows as a value and the expression types
>   `T | error`. (Strict left-to-right
>   evaluation order is *not* normative.) [TE-18.1]

#### Prior S7.7.4

> - **S7.7.4*** Reassignment to a declared `var` carries a diagnostic
>   obligation, in three tiers: compile error where the RHS is provably
>   `T | error`; compile warning where deferred-fallible with a tail to
>   abandon; a runtime report naming the binding that kept its previous value.
>   `x = e ^ { … }` suppresses all three. **No use of a binding ever observes
>   a value left by a failed assignment.** [TE-18]

#### Prior S7.7.5v2

> - **S7.7.5v2*** **Initializer failure inside a loop does not skip its body
>   or abandon the loop.** A `let`/`var` declaration inside the iteration
>   follows S7.7.2v2; later statements and explicit recovery remain reachable.
>   The enclosing `for` collects its body's ordinary result, including an
>   error if that is the result, under the existing collection rules. This
>   initializer ruling does not change accumulator reassignment or settle
>   dedicated typed loop-variable admission. [TE-18.1]

#### Prior S7.7.6

> - **S7.7.6*** Edge sites: element/field stores report via S7.7.4's tiers
>   (containers are not scoped away — documented partial state); a failed
>   module-level reassignment **aborts module initialization**; a cross-frame
>   reassignment from a closure becomes an error return of the inner function,
>   re-originating at the call site. [TE-18]

#### Prior S7.7.7v3

> - **S7.7.7v3*** **Validation errors can be handled as ordinary values.**
>   Recovery after a deferred initializer failure is reachable through the bound
>   outcome: `a ^ { … }`. A handler inside `let a: T = e ^ { … }` handles
>   `e`'s outcome, not a later failure of the annotation check. To recover or
>   propagate before binding a clean `T`, include validation itself in the
>   handled or propagated expression. `or` never recovers an error.
>   [TE-18.1, TE-22]

#### Prior S11.4.1v4

> - **S11.4.1v4*** An annotation is a strict **admission contract**, not a hint.
>   **Three outcomes, never a fourth**: statically proven, statically
>   rejected, or a deferred runtime check whose failure produces a rich error
>   (boundary, expected type, actual value, location) — never null, never a
>   wrong value, never silence. A deferred initializer binds the check's
>   outcome and continues; its effective type preserves the possible error
>   separately from the declared target (S7.7.2v2, SI14v2). The rejected value
>   is never admitted as that target. Reassignment checks before commit and
>   leaves the old value unchanged under its own boundary contract. A
>   `T[]` boundary validates/adopts every logical element before publishing its
>   representation, and a typed array mutation validates its new element before
>   changing the owner. An annotation may drive representation only if its
>   boundary is enforced. An implementation may reuse an admitted array's
>   representation proof only while the actual carrier matches that proof;
>   otherwise it re-admits before optimized access. This internal choice never
>   changes the contract, values, or failure behavior. [S7.7.2, S7.10.6,
>   D3.3.3v3, TE-1, TE-2, TE-4, TE-9]

#### Prior S11.4.6v2

> - **S11.4.6v2*** User-defined types are enforced by the validator: deep, on
>   first crossing, yielding an admitted value or a rich error with a validator
>   path. Initializer bindings retain that outcome under S7.7.2v2; a rejected
>   argument does not enter the callee (S7.7.3).
>   Named map types are **open** — extra fields pass. Constrained types
>   (`T that …`) enforce the base only, for now. [TE-10]

#### Prior S11.4.7v2

> - **S11.4.7v2*** Validation and discharge follow §S7.7–S7.8: deferred
>   initializer checks produce bound outcomes without implicit block exit;
>   containers retain destination-contract admission; `^ { }` handles an
>   error value and postfix `^` explicitly propagates it. [TE-18.1]

#### Prior S12.2.1v2

> - **S12.2.1v2*** `let` bindings and parameters are immutable; only `var`
>   reassigns; shadowing is allowed. An unannotated `var` may change runtime
>   type on reassignment; an annotated `var` constrains every assignment to
>   the declared admission type (S11.4.1v4). Deferred initialization may bind
>   an error outcome under S7.7.2v2; that does not make an invalid value a
>   successful assignment to `T`. Implicit numeric widening at annotated positions is
>   by **exact embedding** only (S4.4.1) — never lossy coercion; assignment
>   never silently corrupts data. [Design_Procedural §3]

#### Prior D2.8.3v2

> - **D2.8.3v2*** **Lane entry is gated on static proof, never on runtime
>   rescue.** Admission is read from the destination contract (§S7.8.1): a
>   provably infallible source enters branch-free; a source only inferable
>   as `T | error` is **not lane-eligible** and stays boxed until
>   discharged — never a checked lane store that might fail mid-mutation.
>   A deferred initializer binds an admitted value or error and continues
>   (§S7.7.2v2); its annotation alone therefore does not prove later uses clean.
>   Its effective outcome stays error-capable until discharge or a valid proof
>   excludes the error on the use path. Only that clean path may use native
>   storage and operators (D2.8.1–D2.8.2). The gating analysis is `may_defect` with
>   fail-closed polarity (D6.1.3): unknown ⇒ defect-capable ⇒ demotion,
>   which is viral through unanalyzed callers — so the effect split is a
>   **prerequisite** for lane routing, not a later optimization, and an
>   unannotated literal that boxes solely for unproven fallibility must be
>   diagnosed rather than silently demoted. [TE-17, TE-18.1; IEH I3, I4]

#### Prior D3.2.2v2

> - **D3.2.2v2*** The **validator is the runtime enforcer** for user-defined
>   types: deep, on first crossing, producing a rich error with a validator
>   path as the failed validation outcome. A deferred initializer binds that
>   outcome without exiting its block (S7.7.2v2); other boundaries consume it
>   according to their own contracts. Constrained types (`T that …`)
>   enforce the base only, for now; `is`, `fn_is`, the emitter, and the
>   validator must consolidate onto the shared foundation (a known
>   three-way divergence today). [TE-10, TE-6 P5, TE-18.1]

#### Prior D3.2.3v2

> - **D3.2.3v2*** Declared and effective types are **separate recorded facts**
>   (declaration on the binding node, inference beside it): an annotation is
>   a strict admission contract (S11.4.1v4). A deferred initializer's effective
>   type includes the validation-error alternative (S7.7.2v2); preserving that
>   outcome does not weaken admission or make the rejected value satisfy the
>   annotation. Inference must never replace validation or authorize a native
>   representation for an error-bearing outcome. [TE-1, TE §8.1, TE-18.1]

#### Prior Type Enforcement round-one binding/storage rule

> 8. **Declarations and reassignment — REVISED 2026-07-30 (user): annotations are contracts;
>    inference flows.** `let x: T = e` establishes only `x: T`: a DEFERRED success binds `T`, and
>    failure yields the boundary error before the binding exists. Reassignment checks before
>    commit and leaves the old value unchanged on failure. `let x = e` has no such declared
>    boundary and infers `e`'s effective type, including `T | error`.
>
>    An explicitly declared error possibility (`T^` or `T | error`) still cannot enter plain
>    `T` without visible discharge/narrowing; that is STATIC-REJECTED. A genuinely dynamic
>    `any` source is the DEFERRED case. Consequently a successfully established annotated
>    `var x: T` may use a `T` carrier; it never needs boxed storage merely to hold a failed
>    check, because the error is not stored in `x`.


### Typed-caret error sets (retired 2026-11-02)

~~Keep `T^E` as a separate error-set convention: constrain user-raised errors
with `E`, allow other ordinary defects implicitly, and defer its relationship
to `T | E`.~~ Replaced by **TE-13.3 / S7.4.2v3 / S11.4.2v3**: remove the
syntax and the implicit ordinary-error allowance, retain `T^`, and use
ordinary unions for particular error outcomes.

The separate notation came from the retired raised/returned distinction.
Keeping it as an alias would duplicate unions; keeping its old exception
would make the written `E` conceal possible ordinary outcomes. The new rule
uses one admission relation regardless of producer. Broad errors remain
expressible as `T^`; exceptional system faults remain separately ruled.
Existing typed-caret contracts require outcome review before narrowing them
to `T | E`. Task/stream notation using an unspecified error set is respelled
with broad `T^` rather than silently narrowed.

The exact provisional follow-up and formal rules replaced by this decision:

> Typed **`T^E` remains a follow-up**. The existing convention constrains
> user-raised errors with `E` while permitting implicit system defects. The
> precise error set after fully unifying explicit returns and raises needs its
> own ruling. Until then, preserve the existing typed-error convention; do not
> claim `T^E` is exactly `T | E`, add a new conversion, or exempt an ordinary
> outcome from acknowledgement.

#### Prior S7.4.2v2

> - **S7.4.2v2*** **`T^` is shorthand for `T | error` in every type position,
>   including function returns.** Equivalent types have the same admission,
>   acknowledgement, and permission to return or raise errors; inference does
>   not create a weaker category. `raise` requires an enclosing return contract
>   admitting its error, not a particular spelling. Typed `T^E` retains its
>   existing error-set convention pending a separate ruling: `R^E` is
>   operationally `R^(E | error)`, with `E` constraining user-raised errors and
>   system defects implicit. This does not exempt any ordinary outcome from
>   acknowledgement or establish `T^E` as exactly `T | E`. [TE-13.2]

#### Prior S11.4.2v2

> - **S11.4.2v2*** Signatures spell both failure dimensions: plain `T`
>   excludes null and error; absence is `T?`, ordinary failure is `T^` or the
>   equivalent `T | error` (S7.4.2v2). **Declared returns enforce admission**:
>   a plain-`T` function with an error-possible return is a compile error —
>   contain the error or disclose it in the return type. An unannotated `fn`
>   retains its implicit `(any \ error, …) -> any \ error` contract and is
>   enforced identically. Unification does not introduce inferred fallible
>   return contracts; when error is disclosed, caller acknowledgement is
>   uniform. [TE-5, TE-13.2]

#### Prior D5.2.1v4

> - **D5.2.1v4*** Function wide-scalar returns use the **companion-lane
>   convention**: a may-be-wide boxed return is the pair `[item, scalar]` —
>   lane 2 a raw 64-bit payload (never a pointer), live iff lane 1 is a
>   **pending Item** (reserved tag `0x1E`, kind in the payload). A pending
>   Item never lives in memory and at most one is live at a time; the
>   caller resolves at first consume (free — the existing tag dispatch),
>   patches on escape (one branch), or forwards on tail return (free).
>   Typed fallible returns use a proven native success lane with a
>   companion error-Item lane. Outcome planning follows the normalized contract:
>   `T^` and `T | error` have identical semantics, irrespective of spelling or
>   whether the body uses `raise` or an ordinary error return (S7.4.2v2).
>   This does not merge the scalar payload lane with the error lane.
>   Classification happens at the wide value's *birth site*, statically —
>   never at boundaries. Lambda and LambdaJS generated functions have no
>   trailing caller-home parameter and no selectable v2 convention; their
>   C-reachable wrappers use `Context::mir_companion_slot`. A separately
>   versioned hosted-language ABI may still request an explicit payload-owner
>   address, but that is a native ownership boundary, not this return ABI.
>   (Reinstates SF14-v1 two-lane returns; retires v1's caller-donated canonical
>   homes — retired-ABI record in Appendix A.) [RV1–RV13, Return_Value §2–§5]


### Implementation status before M1 (historical)

The following source-inspection/probe notes predate M1. Current status is in
Appendix A and the implementation plan; these notes preserve the starting gaps.

#### Earlier Handler convenience status

> **2026-11-02; new syntax and acknowledgement work pending.** The current parser
> only recognizes a body immediately after the postfix caret; `or^` and named
> selectors need lexer/parser, shared AST/type, and interpreter/MIR changes.
> Existing `is (error | null)` was probed on the interpreter with null, an error,
> false, and zero, yielding `[true, true, false, false]`. This verifies the
> classification expression, not conformance of the new acknowledgement rules.
> S7.5.7 rejects a predefined `empty` name; an ordinary user-defined alias
> uses existing type syntax and needs no new keyword implementation.

#### Earlier Success-arm retirement

> **2026-11-02, source inspection only; removal pending.** The production parser
> still accepts the suffix in `lambda/runtime/parser/lambda_parser.c`, and the
> Lambda-owned reference grammar still includes it in `handler_expr` in
> `lambda/tree-sitter-lambda/grammar.js`. AST and evaluator/lowering paths retain
> `value_body`; active demos and fixtures still use the syntax. The implementation
> plan must remove the surface and obsolete arm handling together with caller
> migration and explicit syntax-rejection coverage.

#### Earlier Reassignment outcomes

> **2026-11-02, design only; implementation pending.** TE-18.2 supersedes the
> previous retained-value/block-exit behavior for whole-variable reassignment.
> The shared admission decision, acknowledgement walk, effective type facts,
> interpreter stores, and MIR lowering need coordinated updates. Field/element
> stores retain their separate contract. Qualification must cover errors replacing
> old values, reachable recovery/effects, known rejection versus deferred warnings,
> branch/loop joins, and safe storage on both tiers. No new runtime verification
> is claimed.

#### Earlier Handler results

> **2026-11-02, existing executable, interpreter and JIT REPL probes.** Normal
> value sequencing already preserves a final `null`; declarations leave the
> earlier result unchanged. Procedure handlers do not yet satisfy TE-16.1:
> value-position synchronous handlers yielded `null` on both success and
> recovery, and a final procedure handler after `11` left `11` as the result.
> The suspending value-position path has not been qualified under the new ruling.
>
> `resolve_handler` (`lambda/runtime/build_ast.cpp`) classifies handlers from the
> callee's effect. `eval_handler` (`lambda/runtime/interp.cpp`) and
> `emit_handler_statement_result` (`lambda/runtime/transpile-mir.cpp`) erase
> normal procedure-handler results. `interp_proc_block_last_value`
> (`lambda/runtime/interp_plan.cpp`) and `transpile_content_value`
> (`lambda/runtime/transpile-mir.cpp`) exclude these handlers when selecting the
> block result. These are implementation gaps, not exceptions to the ruling.

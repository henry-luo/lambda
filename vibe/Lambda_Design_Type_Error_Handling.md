# Lambda — Type and Error Handling Design

**Status:** initializer acknowledgement, validation as an expression without
declaration skipping, procedural results, and uniform handler results are
ratified (USER, 2026-11-02); implementation is partial. Further
error-handling design rules will be settled in this document.
**Formal linkage:** S7.5.1v3, S7.5.2v3 (initializer acknowledgement);
S7.7.1v2, S7.7.2v2, S7.7.5v2, S7.7.7v3, S11.4.1v4, S11.4.6v2,
S11.4.7v2, S12.2.1v2, SI14v2 (validation and binding outcomes);
D2.8.3v2, D3.2.2v2, D3.2.3v2 (representation and recorded type facts);
S2.5.3v2, S2.5.4v3, S12.1.2v2 (procedural results);
S7.6.1v5, S7.6.7v5 (uniform handler results).
**Scope:** this is the home for the new error-handling design. Earlier decisions
remain in [Type Enforcement](Lambda_Design_Type_Enforcement.md), pending their
move as the remaining design questions settle. This record extends TE-13,
TE-16, and TE-18 as TE-13.1, TE-16.1, and TE-18.1; it introduces no new ledger
series. Open questions below are discussion topics, not additional rulings.
**Related:** [formal semantics](../doc/Lambda_Formal_Semantics.md),
[procedural design](Lambda_Design_Procedural.md#24-return-and-block-results),
[runtime error handling](Lambda_Design_Runtime_Error_Handling.md).

## TE-13.1 — Initializer acknowledgement by type admission

**Decided 2026-11-02; runtime failure behavior revised by TE-18.1 the same day.
Authority: S7.5.1v3, S7.5.2v3. Implementation pending.**

### Decision

For `let a = e`, a binding acknowledges the initializer's raised error when
its inferred receiving type admits that error outcome. The programmer need
not repeat `| error` in an annotation merely to acknowledge what inference
already knows. The same rule applies to an explicit binding annotation and
to initialization of `var`; reassignment is a separate boundary (S7.7.4).

Acknowledgement means accepting the outcome as a possible bound value. It
does not establish success, recover, or propagate out of the enclosing
procedure. A raised error accepted by the binding becomes an ordinary error
value; subsequent statements can execute. The resulting type must preserve
that error possibility (S7.4.1–S7.4.2).

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
let d: int^ = f()           // equivalent in a value position
```

This decision concerns initializer bindings. It does not grant an initializer
permission to hide unrelated calls inside nested functions or statements.
The existing immediate-expression discipline remains (S7.5.1v3). Extending
the new admission and warning rules to other receiving positions is a
follow-up; existing parameter, return, and reassignment contracts still apply.

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
failure possibility (S11.4.1v4, S7.7.2v2). There is no substitution of `null`,
unchecked value, or stale binding, and no implicit exit from the block.
TE-18.1 replaces the earlier warn-then-skip fallback.

Suggested diagnostic wording (the diagnostic code and final wording remain
to be chosen):

> This initializer may raise an error, but the receiving contract cannot be
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
| **Warning:** allow compilation and enforce the explicit contract at runtime | Fits Lambda's proven/rejected/deferred contract model (S11.4.1v4), preserves dynamic use, and avoids redundant annotations. | A deferred failure is an error outcome that subsequent code must account for; retaining it does not ensure it is inspected. | **Selected, USER 2026-11-02; continuation follows TE-18.1.** |

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
An optional lint for an unused fallible result may be useful, but its adoption
and scope remain open. No new must-inspect rule is introduced here.

## TE-16.1 — Handler results follow procedural sequencing

**Decided 2026-11-02. Authority: S7.6.1v5, S7.6.7v5, S12.1.2v2.**

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

`return e` is a control transfer out of the enclosing procedure, including when
written inside a handler or loop. It does not mean "produce the handler's
fallback". `raise`, `break`, and `continue` keep their ordinary control-flow
targets. None is a normal result update.

### One handler result rule for `fn` and `pn`

The operand is evaluated once. In the one-arm handler, a successful result
passes through unchanged; an error selects the handler body, whose normal
result becomes the handler's result. In the two-arm form the selected arm
provides the result. Errors produced by that arm remain fresh outcomes, not
inputs to the same handler again (S7.6.1v5).

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

## TE-18.1 — Validation produces an outcome; declarations do not skip

**Decided 2026-11-02, USER. Authority: S7.7.1v2, S7.7.2v2,
S11.4.1v4, S7.5.2v3, SI14v2. Implementation pending.**

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
type separately (D3.2.3v2). No invalid original value is passed off as a `T`;
failure yields an error instead. If `T` already admits error, the union need
not add a distinct alternative. A binding proven error-free may retain the
clean type and representation.

This is the deliberate combination chosen by the user: strict checking where
it can be decided, with an error-bearing outcome where admission must be
deferred. It is not a requirement that every annotation be proven before
compilation can proceed, and it is not permission to store an error in a
native `T` slot. Error-bearing outcomes remain represented safely until
discharged or proven error-free (D2.8.1, D2.8.3v2).

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

The decision applies to initializer bindings, including `var` initialization;
it does not authorize invalid writes to typed containers or entering a callee
with a rejected argument. S7.7.3's call-site failure remains an error value
of the call. Reassignment, field/store failures, and dedicated typed loop
variable admission need their own follow-up so that stale values, mutation
failure, and iteration policy are settled explicitly.

## Open design follow-ups

These topics await further decisions; they do not alter current rulings.

| Topic | Question to settle |
|---|---|
| Other receiving positions | Should the initializer's inferred admission and warning policy extend to parameters, returns, reassignment, and container stores? How should that interact with S7.7.3–S7.7.6 and S7.8.1? |
| Validation expression | Choose the surface for value-or-error admission, including runtime-dependent target types; define how it composes with handlers and propagation without a new block-exit mechanism. |
| Mutation and loop bindings | Settle reassignment, field/store failures, and dedicated typed loop-variable admission after retiring initializer skipping; avoid silently treating a retained old value as a successful write. |
| Validation diagnostics | Specify how rejection of an existing error retains or wraps its payload, and how the effective outcome type is displayed when the target is runtime-dependent. |
| Compound initializers | Which nested calls are acknowledged by retaining the outer outcome, and which need their own handling? Clarify the immediate-expression boundary in S7.5.1v3 with examples. |
| Uninspected outcomes | Is an optional unused-fallible-result lint desirable, including procedural results later overwritten under S12.1.2v2? |
| Error-channel simplification | After inferred capture is allowed, which distinctions between `T^E` and `T \| error` remain useful? S7.4.1–S7.4.3 remain in force pending a decision. |
| Diagnostics | Choose the unresolved-admission warning code and wording; define useful locations and suppression without warning on every inferred `any`. |
| Packages and demos | Review the remaining paired bindings, manual error tests, and handlers for concrete friction; use those cases to guide further convenience rules. |
| Design-record migration | Move the remaining error-handling rulings from Type Enforcement with their rationale and superseded alternatives, retaining the TE IDs and formal-spec links. |

## Appendix A — Implementation status

The phased work and qualification criteria are recorded in
[Type and Error Handling Implementation Plan](impl/Lambda_Impl_Type_Error_Handling.md).
All phases are pending; the plan includes the requested `T | error` to `T^`
binding-annotation cleanup and the migration of callers that relied on skips.

### Validation outcomes

**2026-11-02, design only; implementation pending.** The runtime still follows
the prior declaration-containment model. The new behavior requires changes to
deferred-check result routing, effective binding types, and both execution
tiers' handling of error-bearing locals. Error-free native storage remains a
requirement. No validation-expression spelling or new runtime verification is
claimed by this document.

### Initializer acknowledgement

**2026-11-02, source inspection only; new behavior not implemented or tested.**
`contract_acknowledges_error` and the declarator case of
`validate_enforcing_calls_in_expression` in `lambda/runtime/build_ast.cpp`
still use the declared contract and the old explicit-error test. They do not
implement inferred binding acknowledgement or the unresolved-admission warning.
Implementation must distinguish proven admission, proven incompatibility, and
undecidable admission while preserving ordinary runtime contract enforcement.

### Handler results

**2026-11-02, existing executable, interpreter and JIT REPL probes.** Normal
value sequencing already preserves a final `null`; declarations leave the
earlier result unchanged. Procedure handlers do not yet satisfy TE-16.1:
value-position synchronous handlers yielded `null` on both success and
recovery, and a final procedure handler after `11` left `11` as the result.
The suspending value-position path has not been qualified under the new ruling.

**2026-11-03, core result paths implemented; platform qualification ongoing.**
The app-safety worker exposed the same gap while inspecting denied operations.
`resolve_handler` now computes the selected result type for either effect;
`eval_handler` and MIR Direct preserve the selected result; shared block
classification includes procedure handlers. The procedure marker still selects
the local fault-recovery regime. It no longer suppresses a value.

`SecurityRuntimeTest.ProcedureHandlersExposeDenialsAndResumeWithTheirSelectedValues`
passes on interpreter and JIT, also with forced GC and poisoned frees. It covers
owned error fields in recovery, arrays/bindings, successful pass-through,
two-arm selection, final-handler block results, final-null replacement and a
value handler across a suspending read (**S7.6.1v5, S7.6.7v5, S12.1.2v2**).
The safety implementation ledger records aggregate and signed-worker gates.
This focused evidence does not claim exhaustive async/fault/platform coverage.

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

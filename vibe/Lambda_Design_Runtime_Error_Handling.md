# Lambda — Runtime Error Handling Design

**Status:** DESIGN SETTLED; implementation landed 2026-08-17.

**Date:** 2026-08-17

**Formal authority:** **D1.4v3**, **DI15v2**, **D5.1.4**, **D5.2.1v3**,
**D6.3.3**, and **D8.4.3v2** in
[`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md), with the
language-visible failure taxonomy and handlers in **S7.4–S7.6** and native
system-fault rules in **S7.11** of
[`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md).

This document is the cross-language runtime contract for errors and exceptions
in Lambda, LambdaJS, and every other language hosted on the Lambda runtime. The
formal specification wins on disagreement; this record owns the detailed
rationale, lowering shape, conformance rules, and migration plan.

Related documents:

- [`doc/Lambda_Error_Handling.md`](../doc/Lambda_Error_Handling.md) describes
  Lambda's user-facing `error`, `raise`, `T^E`, propagation, and handler syntax.
- [`doc/dev/lambda/LR_10_Error_Handling.md`](../doc/dev/lambda/LR_10_Error_Handling.md)
  inventories Lambda's current runtime error representation and guard helpers.
- [`Lambda_Impl_Error_Handling (done).md`](<impl/Lambda_Impl_Error_Handling (done).md>)
  records the earlier Lambda-specific implementation work.
- [`Lambda_Impl_Error_Rework.md`](impl/Lambda_Impl_Error_Rework.md) is the phased
  implementation and conformance plan for this design, including resume-safe
  lowering of the existing postfix procedure-call handler.
- [`Lambda_Design_Exec_Recovery.md`](Lambda_Design_Exec_Recovery.md) owns the
  temporary non-local recovery mechanism for native system faults only.

---

## 1. Why this document exists

The existing documents answer separate questions: Lambda syntax, current
Lambda implementation, LambdaJS lowering, and native fault recovery. None of
them previously stated one host-wide rule for how an ordinary error or
language exception moves through native/generated call frames.

That rule is now explicit:

> A language failure propagates by normal return through every live activation.
> Every activation observes and routes the failure, runs its own cleanup, and
> returns an explicit completion to its caller. No language failure skips a
> native or generated frame.

In this document, **structured unwind** means this frame-by-frame sequence of
ordinary returns and intra-function branches. It does **not** mean C++ exception
unwinding, platform SEH unwinding, or `setjmp`/`longjmp`.

### REH-D1 — One strict boundary between failures and faults

Language failures and native system faults are different mechanisms:

| Category | Examples | Runtime transport | May skip frames? |
|---|---|---|---|
| Lambda value/raised error | `error(...)`, `raise`, failed type check, I/O error | returned ERROR-tagged `Item` or companion error lane | no |
| LambdaJS exception | `throw`, runtime `TypeError`, Promise rejection | returned ERROR-tagged `Item`, generated completion routing, stored async completion | no |
| Hosted-language exception | guest `throw`/raise/trap defined by its language | guest lowering plus explicit host completion adapter | no |
| Structured control completion | `return`, `break`, `continue`, generator completion | generated branch/state-machine result inside the owning language | no |
| Native system fault | S7.11.1v2 stack/side-stack exhaustion, OOM, runtime boundary defect | temporary `LambdaRecoveryFrame` fault channel | temporarily yes |
| Test/process containment | timeout, crash worker termination | harness/process policy, never a language completion | outside language semantics |

An ordinary failure must never be promoted to a native fault merely to avoid
adding a caller check. Conversely, a native fault is not added to every
function type merely because its temporary recovery landing later materializes
an ERROR-tagged `Item`.

---

## 2. Core propagation model

### REH-D2 — Every frame participates

When a callee fails, control returns to its immediate caller. That caller must
do exactly one of the following in its own activation:

1. handle the failure locally;
2. run required `catch`/`finally`/defer/iterator/module cleanup and then handle
   or propagate it;
3. copy or preserve the same completion in durable async state and return to
   the scheduler; or
4. branch to its own error epilogue and return the completion to its caller.

It may not jump directly to an ancestor handler or execution boundary.

```text
fallible helper or callee
    returns explicit failure
immediate caller frame
    tests failure before consuming success
    runs caller cleanup / handler / finally
    returns explicit failure if still unhandled
next caller frame
    repeats the same contract
execution boundary
    reports or stores the unhandled completion
```

Inlining does not violate this rule because no native activation exists for
the inlined callee. A tail call is legal only after the caller has completed
the epilogue it would have run before an ordinary return; tail-call lowering
must not erase required failure cleanup.

### REH-D3 — Failure is explicit in the call ABI

The result shape depends on representation, but failure is always explicit:

- A boxed fallible callable returns one `Item`: success or ERROR-tagged failure.
- A proven typed/native success lane uses the companion ERROR-`Item` lane of
  D5.2.1v3; the caller tests that lane before reading the native success lane.
- A generated-callable helper returning a raw pointer, scalar, or `void` is
  fallible only through an explicit result adapter. Raw result helpers in the
  runtime catalog are otherwise required to be infallible.
- A low-level internal helper may use a checked `bool`/null result inside one
  native function, but its immediate owner must convert that result to the
  explicit completion before returning across a runtime/generated boundary.

The following are not failure ABIs:

- `Context::last_error`, TLS flags, or pending-exception slots;
- a magic scalar/null sentinel whose caller can consume it as success;
- logging plus `ItemNull`;
- a C++ exception or platform unwind;
- a recovery-frame jump.

`last_error` may mirror diagnostics, but it may never be the control signal
that tells a caller whether a call failed. The returned completion is
authoritative.

### REH-D4 — Test before success consumption

Every fallible call site tests the completion before unboxing, arithmetic,
property access, mutation, storing into an error-free lane, or invoking another
success-only helper. This is the runtime counterpart of D2.8.2.

Where static proof establishes infallibility, the check may be omitted. Missing
or unknown effect metadata is fallible/defect-capable, never trusted clean.

### REH-D5 — Preserve the failure identity

Propagation returns the same rich error/exception carrier whenever possible.
Wrapping is an explicit language or boundary operation, not an incidental
effect of crossing a frame. Source/stack metadata may be enriched lazily, but
the control path cannot replace a specific error with a generic sentinel after
the specific carrier already exists.

If diagnostic enrichment itself cannot allocate, S7.11's OOM fault rule owns
the escalation. That is a native system fault, not ordinary error propagation.

---

## 3. Frame shape and cleanup

### REH-D6 — One structured error epilogue per generated function

A generated function has one logical error epilogue. Multiple failure edges may
branch to it inside the same activation. The epilogue performs, in order:

1. language-mandated cleanup still owned by the frame;
2. write-back/iterator/finally/module bookkeeping required by the language;
3. root-frame and number-home cleanup under D5.1.4;
4. restoration of frame-local runtime state; and
5. ordinary return of the explicit failure completion.

The normal and error epilogues may share cleanup blocks. What matters is that
every active frame executes its own cleanup; an ancestor does not repair state
for an ordinary failure after skipped frames.

### REH-D7 — Native C/C++ helpers return normally

Fallible C/C++ helpers must return through normal language semantics. Therefore:

- automatic destructors run normally;
- `RootFrame`/`Rooted` ownership is released normally;
- locks, file handles, temporary buffers, and catalog activation state are
  released by the frame that acquired them;
- no cleanup contract may cite a later recovery checkpoint for an ordinary
  error.

Lambda runtime code does not use C++ `throw`/`catch` as an implementation of a
language failure. If an external library has its own internal exception or
`setjmp` mechanism, the immediate adapter must contain it entirely within that
one foreign call and convert the outcome to an explicit completion before
returning to Lambda-owned code. No foreign unwind may cross a Lambda-owned
frame.

---

## 4. Lambda lowering

### REH-D8 — Lambda errors use returned values and local branches

Lambda follows S7.4 and S7.6:

- `error(...)` constructs an error value.
- Returning `T | error` returns that value as ordinary data.
- `raise e` places `e` in the declared raised-error completion and branches to
  the current function's error epilogue.
- Postfix `e^` tests the operand and branches to the current function's error
  epilogue on failure.
- `e ^ { ... }` tests the operand and branches to a handler label in the same
  generated activation; an unhandled failure from the handler then follows the
  ordinary function error epilogue.
- In statement position, `pn_call() ^ { ... }` tests the returned procedure
  completion after the call or async resume, runs the body with `^` bound to an
  error, and continues after the statement when the body completes normally.
  A success result is discarded.
- Calls into runtime helpers receive and test an ERROR-tagged `Item` or the
  companion error lane before consuming the success lane.

No Lambda `raise`, propagation operator, handler, declaration-boundary skip, or
runtime error code may call `lambda_recovery_frame_raise_fault()` as its normal
implementation. A local recovery frame may be armed solely so an S7.11 native
fault can reach an eligible procedural fault boundary; the ordinary operand
error path must still use the returned lane.

The statement form keeps one spelling for synchronous and possibly-suspending
procedure calls. Its **ordinary-error path never depends on fault recovery**:
the returned ERROR `Item` is tested in the current activation, or is stored in
task state and tested after resume. No `LambdaRecoveryFrame`, native frame
address, or `jmp_buf` becomes part of that ordinary continuation.

Native S7.11 faults retain their temporary carve-out. A synchronous handler may
land through its live local fault frame. After suspension, the task fault
boundary materializes the fault into durable task state and selects the nearest
active procedural handler state; once materialized, routing returns to the
ordinary explicit completion path. The native jump may skip frames under
REH-D13, but no language error does so.

---

## 5. LambdaJS lowering

### REH-D9 — JavaScript exceptions are generated completions

LambdaJS keeps ECMAScript `throw`/`try`/`catch`/`finally` semantics while using
the host's explicit completion ABI:

- `js_throw_value()` returns an ERROR-tagged carrier containing the thrown JS
  payload; it does not throw a C++ exception or jump.
- A direct source `throw` branches to the nearest generated catch/finally label
  in the current JS activation, or to that activation's error epilogue.
- A fallible helper or JS callee returns the carrier normally. The immediate
  generated caller tests it and routes it through its own lexical
  catch/finally or error epilogue.
- `finally` executes in the frame that owns it and may preserve or replace the
  incoming completion according to ECMAScript semantics.
- Error identity and primitive thrown payload identity survive frame-by-frame
  propagation.

There is no pending JS exception flag, exception polling side channel, C++
exception, or `setjmp` implementation of ordinary JS `throw`.

Promise rejection is an asynchronous language completion, not a native fault.
It is stored in Promise/task state and delivered as resume input; it never
targets a native frame that existed before suspension.

---

## 6. Other hosted languages and Jube

### REH-D10 — Language semantics stay local; transport stays explicit

A hosted language may define exceptions, result objects, traps, resumable
conditions, or no exception construct at all. Its front end and generated code
own those semantics. The Lambda runtime imposes only the transport rule:

- a failure crossing a generated/native call boundary is an explicit
  completion;
- the immediate caller routes it in its own activation;
- a guest-to-host or host-to-guest adapter converts payload representation but
  returns normally;
- no guest installs a native unwinder that can cross host frames;
- no guest reuses LambdaJS lexical exception state or a hidden host pending
  flag.

Catalog metadata must state whether a callable is infallible, returns a merged
Item completion, or uses a typed companion error lane. Unknown metadata is
fallible.

### REH-D11 — Foreign and module boundaries are ordinary returns

A module initializer, native module call, `eval`, import, or hosted guest entry
returns an explicit failure to its immediate owner. Transactional owners then
rollback or mark initialization failed in their own frame before returning the
same failure outward.

An ordinary module or guest failure must not use a transaction recovery frame
to skip the initializer. Transaction recovery frames exist only for native
system faults that have already made normal return unsafe.

---

## 7. Async, callbacks, generators, and cancellation

### REH-D12 — Suspension converts a live completion into durable state

No ordinary failure path depends on a native frame surviving suspension:

- A task poll returns `DONE`, `READY`, or `PARKED` plus an explicit result.
- A failure completes the task or is stored in its state before the poll
  returns to the scheduler.
- Promise rejection is queued/stored and routed when the generated JS state
  machine resumes.
- Generator `throw`/`return` is resume input handled by the state machine, not
  a jump to the activation that originally yielded.
- Cancellation follows the task/function's declared completion protocol and
  runs structured cleanup in each resumed frame.
- A suspended `pn_call() ^ { ... }` stores the call completion in rooted task
  state. On resume, the same generated activation tests it and enters the
  postfix handler exactly once if it is an ordinary error; success skips the
  body and continues after the statement.
- Callback dispatchers inspect the callback's returned completion and apply
  the owning language's reporting/propagation policy before returning.

This rule remains true even while native system-fault recovery is retained:
`LambdaRecoveryFrame` never survives a scheduler yield under D6.3.3.

---

## 8. Native system-fault carve-out

### REH-D13 — `LambdaRecoveryFrame` is fault-only and temporary

For the time being, native system faults may use non-local recovery because
normal return can be impossible after stack exhaustion or during OOM/resource
failure. The allowed reasons are the closed S7.11.1 set:

- native stack overflow;
- side-stack exhaustion;
- out of memory;
- compiler-inserted runtime boundary defect.

Structural-equality depth exhaustion is intentionally excluded from this
carve-out: it is a language-visible ordinary error and follows REH-D2 through
the explicit completion lane.

The carve-out has hard boundaries:

1. The origin is a `LambdaFaultReason`, never an ordinary `LambdaErrorCode`, JS
   thrown value, guest exception, I/O failure, parse/type error, cancellation,
   or Promise rejection.
2. Only `LambdaRecoveryFrame` and the execution-recovery design may perform the
   non-local transfer.
3. A jump may land only on the same thread and never across a scheduler yield.
4. The landing restores every skipped runtime watermark before observing or
   materializing the fault.
5. Once materialized, any further propagation uses the ordinary explicit
   completion path; it is not re-thrown by another jump.
6. No new language feature may depend on this carve-out. It is retained for
   native fault survival and will be revisited separately.

Actual arbitrary memory faults remain fail-stop under S7.11.4. Test-worker
crash/timeout containment is not a language exception mechanism and must never
make such failures catchable by Lambda or a hosted language. Process isolation
is the preferred long-term containment boundary.

---

## 9. Rejected implementation patterns

The following violate D1.4v3/DI15v2 for ordinary failures:

- installing a `setjmp` target for Lambda `raise` or `^` propagation;
- implementing JS `throw` by jumping to the nearest native catch frame;
- throwing a C++ exception through generated MIR or runtime helpers;
- storing a pending exception in TLS/context and returning a plausible success
  value;
- polling a global exception flag after arbitrary calls;
- converting an ignored error to `null`, zero, false, or an unchanged object;
- jumping directly from a helper to a module, task, or script boundary;
- using a system-fault reason as a generic escape hatch for an ordinary error;
- retaining a pointer to a handler/native frame across `await`, `yield`, or a
  scheduler poll.

---

## 10. Mechanical enforcement

### REH-D14 — The distinction must be machine-checked

The implementation is conforming only when the following gates exist:

1. **Jump allowlist.** A lint/CI inventory rejects `setjmp`, `longjmp`,
   `sigsetjmp`, `siglongjmp`, SEH recovery, and `LambdaRecoveryFrame` use
   outside the native-fault and explicitly test-only containment files.
2. **Typed fault API.** Recovery origins accept only `LambdaFaultReason`; no
   overload accepts a general error `Item`, JS payload, or arbitrary error code.
3. **Catalog completeness.** Every callable helper declares infallible, merged
   Item completion, or companion error lane. Missing metadata fails closed.
4. **Emitter checks.** Every fallible call is followed on all paths by a lane
   test before success consumption. MIR verification rejects an unchecked use.
5. **Epilogue tests.** Nested native/generated call tests verify that each frame
   runs cleanup exactly once while an error propagates through it.
6. **Identity tests.** Lambda, LambdaJS, and guest boundary tests verify that
   the same error/thrown payload reaches the final handler.
7. **Async tests.** Await, Promise, task, callback, generator, and cancellation
   tests verify durable completion delivery with no retained native target.
8. **Fault separation tests.** An ordinary error cannot select a recovery
   frame, while each S7.11 reason can reach only an eligible same-thread fault
   boundary.

Useful cleanup witnesses include root/number watermarks, module transaction
state, iterator-close counts, `finally` side effects, lock/handle ownership,
and callback-dispatch depth.

---

## 11. Implementation audit and migration result

The frame-by-frame design is landed for the active MIR Direct, AST interpreter,
LambdaJS, Jube, and hosted-language paths. The audit result is:

| Area | Landed result | Authority |
|---|---|---|
| Lambda ordinary failures | Returned ERROR/companion completions are checked in the immediate activation; handler bodies are outside the consumed fault-only frame. | D1.4v3, REH-D2, REH-D8 |
| Suspended `pn` handlers | `pn_call() ^ { ... }` is a statement-position state-machine edge; its completion is rooted and selected after resume exactly once. | S7.6.7v3, REH-D12 |
| Native faults | The scheduler materializes only S7.11 faults, routes them to a task-owned durable target, and never carries a `jmp_buf` across a park. | D6.3.3, S7.11, REH-D13 |
| AST interpreter | Handler evaluation and current-error scope use explicit frame signals and rooted scratch slots; recursion-budget errors are ordinary rich completions. | D8.1.1v2, S7.6.1v4 |
| LambdaJS | The existing explicit completion/error lane remains the sole ordinary exception path; the JS entry boundary is fault-only. | D8.4.3v2, REH-D9 |
| Jube/host adapters | Guest/module failures are returned at the immediate adapter boundary; recovery frames remain limited to host execution/transaction containment. | D8.4.3v2, REH-D10/REH-D11 |
| Enforcement | `test/error_handling/check_recovery_boundaries.py` rejects new ordinary recovery sites and checks the typed fault API inventory. | REH-D14 |

No phase broadens C2MIR: the legacy path remains frozen under D1.6. The
remaining `setjmp`/`longjmp` sites are native S7.11, test/process containment,
or vendored implementation code excluded by the frozen-path rule; none is an
ordinary language failure route.

---

## 12. Current implementation map

The current mechanisms relevant to the audit are:

| Concern | Current implementation area |
|---|---|
| Lambda error representation and guards | `lambda/runtime/lambda-error.*`, `lambda/lambda.hpp`, `lambda/runtime/lambda-eval.cpp` |
| Lambda MIR error/handler lowering | `lambda/runtime/transpile-mir.cpp` |
| Interpreter structured completion | `lambda/runtime/interp.cpp`, `lambda/runtime/interp.hpp` |
| LambdaJS completion routing | `lambda/js/js_mir_completion.cpp`, `lambda/js/js_mir_statement_lowering.cpp` |
| LambdaJS error carrier | `lambda/js/js_runtime_state.cpp` (`js_throw_value`) |
| Async task completion | `lambda/runtime/concurrency.cpp` |
| Native fault recovery | `lambda/runtime/recovery_frame.*`, `lambda/runtime/lambda-stack.cpp` |
| Execution boundaries | `lambda/runtime/runner.cpp`, LambdaJS/Jube entrypoint files |
| Test-process containment | `lambda/main.cpp`, test-worker drivers |

This source map is descriptive and may drift. The REH decisions and formal
IDs, not line numbers, define conformance.

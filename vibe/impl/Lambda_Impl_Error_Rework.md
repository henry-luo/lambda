# Lambda — Runtime Error Handling Rework Implementation Plan

**Status:** IMPLEMENTED — all required runtime phases landed 2026-08-17;
validation and known unrelated baseline dispositions are recorded below.

**Date:** 2026-08-17

**Primary objective:** ordinary language failures in Lambda, LambdaJS, and
every other language hosted by the Lambda runtime return through every active
native/generated frame. No ordinary error, exception, rejection, or
cancellation uses `setjmp`/`longjmp` or skips a native frame.

**Required Lambda handler syntax:** this plan preserves the existing form:

```lambda
pn_call() ^ {
    // `^` is the current error
    error_handler(^)
}
```

No alternate procedure-handler spelling is introduced. No new grammar rule or
AST node for one is to be added.

**Formal authority:**

- **D1.4v3** and **DI15v2** — every language failure returns through every
  active frame; no language failure skips a frame.
- **D5.1.4** and **D5.2.1v3** — precise-root cleanup order and the companion
  error lane for native scalar returns.
- **D6.3.3** — a native recovery frame never survives suspension or crosses a
  thread boundary.
- **D8.4.3v2** — every hosted-language helper uses an explicit completion ABI.
- **S7.4**, **S7.6.1v4**, **S7.6.3v2**, **S7.6.7v3**, and **S7.11** — Lambda
  error channels, handler/propagation behavior, suspension, and the closed
  native-fault carve-out.
- **REH-D1–REH-D14** in
  [`Lambda_Design_Runtime_Error_Handling.md`](../Lambda_Design_Runtime_Error_Handling.md)
  — detailed cross-language transport and enforcement design.

The formal documents win if this plan drifts. Any implementation discovery
that requires a semantic change must first revise the applicable `S#`/`D#`
ruling and this plan; it must not be hidden in code.

---

## 1. Scope and non-goals

### 1.1 In scope

1. Separate ordinary language failure transport from native system-fault
   recovery in APIs, AST metadata, MIR lowering, interpreter behavior, async
   state, tests, and mechanical checks.
2. Make every ordinary failure an explicit returned completion:
   ERROR-tagged boxed `Item`, or D5.2.1v3's companion ERROR `Item` beside a
   native success lane.
3. Give every generated Lambda and hosted-language function one logical error
   epilogue, reached only by an intra-function branch.
4. Audit native helpers so a fallible helper returns to its immediate caller;
   its caller checks failure before consuming the success lane.
5. Preserve LambdaJS `throw`/`try`/`catch`/`finally` semantics using generated
   completions, not C++ exceptions, pending-exception flags, or jumps.
6. Audit Jube/foreign adapters so guest failures are converted at the adapter
   and returned one frame at a time.
7. Keep the existing `e ^ { ... }` grammar and support
   `pn_call() ^ { ... }` in statement position for synchronous and
   possibly-suspending procedure calls.
8. Retain non-local native recovery temporarily only for the closed S7.11
   system-fault set, with typed origins and same-thread landing rules.
9. Add CI/lint/test gates that prevent ordinary errors from regressing onto the
   fault channel.

### 1.2 Out of scope

- Removing native system-fault recovery entirely. Stack exhaustion and OOM
  recovery remain a future design problem under REH-D13.
- Making arbitrary memory faults catchable. Invalid memory access remains
  fail-stop under S7.11.4.
- Extending or fixing the frozen C2MIR path. All compiler work is MIR Direct
  and the AST interpreter; `--c2mir` may remain unsupported.
- Patching MIR, Tree-sitter, or another vendored dependency.
- Adding `try`/`throw`/`catch` syntax to Lambda.
- Adding another procedure-handler spelling or changing handler precedence.
- Replacing Lambda's `T | error` and `T^E` type semantics.

---

## 2. Locked behavior

### 2.1 Ordinary frame-by-frame propagation

For an ordinary failure, every live activation executes this shape:

```text
callee/helper returns an explicit failure
    ↓
immediate caller tests the completion before success use
    ↓
caller handles it locally, stores it for resume, or enters its error epilogue
    ↓
caller runs its own cleanup and returns the completion normally
    ↓
next caller repeats
```

There is no native stack skip. "Unwind" in this plan means ordinary returns
plus local generated branches, not C++ exception unwinding.

### 2.2 Lambda completion representation

The implementation keeps the established ABI rather than inventing another
result object:

| Callee result shape | Success | Failure | Caller rule |
|---|---|---|---|
| Boxed | normal `Item` | ERROR-tagged `Item` | test tag before use |
| Native scalar/pointer lane | native value | companion ERROR `Item` | test companion before native value |
| Async procedure/task | rooted durable result | rooted ERROR `Item` | test after resume |

The failure carrier is authoritative. `Context::last_error`, logs, TLS, null,
zero, and stale output homes are not control signals.

### 2.3 `pn_call() ^ { ... }`

The existing handler has these required semantics in statement position:

1. Evaluate the complete call expression exactly once.
2. In the shown one-arm form, if it succeeds, discard the procedure result and
   continue after the handler.
3. If it returns or raises an ordinary error, enter the handler body in the
   same caller activation with `^` bound to that error.
4. If the handler body completes normally, continue after the handler.
5. `return`, `raise`, `break`, and `continue` in the body retain their normal
   meaning.
6. An error created by the handler body is a fresh outcome; the same handler
   does not consume it again.
7. Nested handlers restore the enclosing current-error binding.
8. The callee's static function kind must be `pn`; a procedure handler remains
   statement-only.
9. The ordinary-error behavior is identical before and after a scheduler
   suspension.
10. The existing optional `~ { value_body }` arm remains supported: success
    executes it as a statement body with `~` bound to the procedure result.

No syntax change is needed. `handler_expr` already parses the form; the AST
builder classifies it as `AST_NODE_HANDLER_STAM` when the operand is a `pn`
call in statement position.

### 2.4 Native-fault behavior at a procedure handler

S7.11 faults stay separate from the ordinary path:

- A synchronous `pn_call() ^ { ... }` may retain a live, fault-only local
  `LambdaRecoveryFrame` while evaluating the call operand.
- That frame must never receive `raise`, a returned ERROR `Item`, I/O failure,
  cancellation, guest exception, or Promise rejection.
- A possibly-suspending handler must not retain that frame or its `jmp_buf`.
  Instead, generated async state registers a durable procedural fault target.
- If a native fault aborts a later task poll, the scheduler's existing
  execution recovery boundary materializes the S7.11 fault into a task-owned
  ERROR `Item`, discards abandoned child async frames, and resumes the nearest
  registered handler state.
- The handler state clears its fault target before running the body. A fault in
  the handler body therefore propagates outward and is not re-caught.
- If no durable procedural handler is active, the task completes with the
  recovered fault as it does today.

Only the initial native transfer may skip frames. Once the fault is
materialized, all further routing follows the explicit completion path.

---

## 3. Current implementation findings

The rework started from a partially-correct architecture, not a blank slate.
The following inventory records the pre-rework defect and the landed result.

| Area | Current useful mechanism | Gap to close |
|---|---|---|
| Grammar | `handler_expr` is a postfix-primary rule in `grammar.js` | no grammar change; add regression coverage only |
| AST | `AstHandlerNode`, `AST_NODE_HANDLER_EXPR/STAM`, `build_handler()` | the existing `is_statement` bit remains the single handler-mode classification; async fault state is assigned only to statement `pn` handlers |
| Enforcement | `validate_enforcing_calls_in_expression()` recognizes a handler operand as immediate acknowledgment | preserve E228 behavior for `pn_call() ^ {}` through refactoring |
| Concurrency analysis | `validate_handler_await_node()` detects a suspending operand | statement `pn` handlers are admitted to durable state-machine lowering; value-producing `pn` handlers retain E221 |
| MIR handler | `transpile_handler()` tests an ERROR-tagged boxed operand and branches locally | synchronous procedure statements call `transpile_local_fault_expression()`; ordinary and fault paths are too closely coupled |
| MIR async | `async_emit_invoke_resume_point()` and async-frame slots preserve call continuation | statement `pn` handlers now use a durable task-owned fault target; value-producing suspended `pn` handlers remain rejected |
| Interpreter | `EvalSignal` returns control without language longjmp | both handler node forms execute through rooted `eval_handler()` and current-error scope |
| Task runtime | task results and async slots are rooted; task polls have a fault-only execution recovery frame | a materialized native fault selects the nearest linked durable handler and re-enters its generated state |
| LambdaJS | `js_throw_value()` plus `js_mir_completion.cpp` use an ERROR carrier and generated catch/finally labels | complete helper/callback/module audit and mechanical no-jump proof remain |
| Jube | callable metadata includes `can_raise`; host adapters have explicit entry ABI | completion metadata and error-lane checks are enforced at the audited hosted boundaries |
| Recovery | `LambdaFaultReason` and `LambdaRecoveryFrame` distinguish fault origins | remaining recovery uses are classified by the machine allowlist; ordinary completion has no recovery-raise edge |

The implementation must preserve working explicit-return paths and remove only
the coupling or bypasses that violate D1.4v3/DI15v2.

---

## 4. Target implementation structure

### 4.1 Explicit handler mode in the AST

Reuse `AstHandlerNode`; do not create a third near-identical handler node.
The existing `is_statement` bit plus the
`AST_NODE_HANDLER_EXPR/STAM` node kind already encode the two semantic modes;
the landed implementation keeps that compact representation. Shared helpers
answer:

- whether the selected arm contributes a value;
- whether success is passed through or discarded;
- whether the operand must be a `pn` call;
- whether a local **fault-only** frame is eligible;
- whether async lowering needs a durable fault target.

Do not derive ordinary error transport from `mt->in_async_proc`. Async status
chooses storage/lifetime, not error semantics.

### 4.2 One logical error epilogue per generated function

Extend the per-function MIR emission context with an explicit error destination
and carrier home. Add shared helpers rather than duplicating tag/lane logic:

- box/normalize a fallible call completion;
- test ERROR before success consumption;
- write the current frame's returned error carrier;
- branch to the current frame's error epilogue;
- merge normal and error cleanup where safe.

For a `can_raise` native-return function, the epilogue writes the companion
error lane and returns a neutral, unread success value. For a boxed function,
it returns the ERROR `Item`. The immediate caller checks the lane before using
the neutral success value.

### 4.3 Ordinary handler lowering

`transpile_handler()` remains the common lowering for expression and statement
handlers, but its first operation is always ordinary operand evaluation:

```text
operand_completion = evaluate operand once
if operand_completion is ERROR:
    clear active fault target, if any
    bind handler-local ^
    execute error body
    restore outer ^
else:
    clear active fault target, if any
    pass through / execute value arm / discard according to handler mode
continue
```

`transpile_local_fault_expression()` is not the implementation of this flow.
It is renamed/scoped as a native-fault wrapper and used only when the handler
mode and suspension analysis prove that a live synchronous fault checkpoint is
valid. The ordinary returned ERROR edge always comes from the call result.
The selected statement body propagates a newly created ERROR, while an
assignment such as `value = ^` consumes the handler input as data.

### 4.4 Durable async fault target

Ordinary errors need only existing result/resume slots. Native-fault parity for
a suspended procedure handler needs task-owned metadata because a local
`jmp_buf` is dead after a poll returns.

Add one active fault-target descriptor per `LambdaAsyncFrame` (one handler
operand can be active in an activation at a time):

```text
active                    boolean
handler_state             generated resume-state id
error_slot                rooted Item slot receiving the materialized fault
scope_base                task-scope checkpoint owned by the activation
```

The generated state machine:

1. allocates/reserves the rooted error slot;
2. registers the descriptor immediately before entering a possibly-suspending
   handler operand;
3. leaves it active while the call is parked;
4. clears it on ordinary success/error before selecting either arm;
5. emits a resume state that loads the rooted fault `Item`, binds `^`, and
   enters the handler body exactly once.

The task poll fault landing searches the async-frame stack from innermost to
outermost. If it finds a descriptor, it:

1. publishes the fault into the task-owned `LambdaFaultRecord`;
2. destroys/unregisters abandoned async frames above the target using their
   existing frame destroy callbacks;
3. restores task-scope and root metadata to the target's recorded boundary;
4. stores the materialized ERROR `Item` in `error_slot`;
5. sets the target frame's state to `handler_state`;
6. clears the descriptor so the body cannot catch itself; and
7. requeues the task as READY instead of completing it.

If any invariant fails, convert the condition to
`LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT` and complete the task; never resume with
partially-restored runtime state.

This descriptor is not an ordinary exception stack. Ordinary errors never
query it, and it contains no native stack address or jump buffer.

---

## 5. Phased implementation

Every phase must be separately buildable and testable. Do not combine grammar,
runtime ABI, LambdaJS, and fault-runtime changes in one unreviewable patch.

### P0 — Baseline, inventory, and proof fixtures

**Purpose:** freeze the current evidence before changing control flow.

Actions:

1. Capture the exact current failing tests, including the reported 11 failures,
   with command, tier, diagnostic, and root-cause classification. Do not bless
   failures caused by E221/E308 as new expected behavior.
2. Inventory every `setjmp`, `sigsetjmp`, `longjmp`, `siglongjmp`, SEH recovery,
   `LambdaRecoveryFrame`, and `lambda_recovery_frame_raise_*` reference under
   `lambda/` and relevant test harnesses.
3. Classify each reference as:
   - S7.11 native fault;
   - test/process containment;
   - foreign-library containment that does not cross Lambda frames; or
   - ordinary-error violation.
4. Inventory fallible helper ABIs: boxed `Item`, companion lane, raw result,
   diagnostic-only, or unknown.
5. Add nested cleanup-witness test helpers before changing lowering. Witnesses
   count function epilogues, root restoration, task-scope cleanup, iterator
   close, and JS `finally`; no test helper may use ordinary-error longjmp.
6. Record a release-build size baseline for `libtree-sitter-lambda.a` even
   though no grammar growth is planned, and record debug/release binary sizes
   separately.

Artifacts:

- a committed test/CI allowlist for legitimate jump sites;
- a helper ABI inventory or generated catalog check;
- initial negative tests proving an ordinary error cannot select a recovery
  frame;
- a table mapping all 11 failures to a later phase.

Exit gate: every jump origin and every failing test has an owner; no
unclassified recovery use remains.

### P1 — Preserve grammar; make AST intent explicit

**Files:**

- `lambda/tree-sitter-lambda/grammar.js` (verification only)
- generated `lambda/tree-sitter-lambda/src/parser.c` and
  `lambda/runtime/ts-enum.h` (generation only if needed)
- `lambda/runtime/ast-core.hpp`
- `lambda/runtime/build_ast.cpp`
- `lambda/runtime/ast.hpp`
- `lambda/runtime/emit_ast_dump.cpp`
- `lambda/runtime/interp_plan.cpp`

Actions:

1. Do **not** add a new grammar rule. Keep `handler_expr` under the existing
   postfix-primary precedence and keep the caret owned by that rule.
2. Add parse/AST fixtures proving `pn_call() ^ { ... }` still becomes one
   handler over one call, including member, imported, indirect, and system `pn`
   callees.
3. Keep the existing `is_statement` bit and update `build_handler()` after
   `handler_operand_is_proc()` and statement-position analysis. The node kind
   plus this bit already provides the two semantic modes; adding an enum would
   duplicate the same classification without reducing ambiguity.
4. Preserve one `AstHandlerNode` and the existing
   `AST_NODE_HANDLER_EXPR/STAM` split. Do not duplicate walker cases.
5. Update every handler visitor: type propagation, enforcing-call validation,
   effect analysis, AST walking, S-expression emission, frame planning, and
   diagnostics.
6. Keep E228 immediate acknowledgment: the call under
   `pn_call() ^ { ... }` is handled, but calls in its handler body start a new
   failure domain.
7. Keep the procedure value-context diagnostic. This plan does not make a
   value-producing expression out of a `pn` handler.
8. Change `validate_handler_await_node()` only after P3/P4 are ready. Until
   then, retain E221 so P1 cannot expose an unsafe path.

Exit gate: CST shape and parser archive size do not regress; AST dumps identify
handler mode explicitly; all old handler tests behave unchanged.

### P2 — Normalize Lambda ordinary completion routing

**Files:**

- `lambda/runtime/transpile-mir.cpp`
- `lambda/runtime/mir_emitter_shared.hpp`
- `lambda/runtime/lambda-eval.cpp`
- `lambda/runtime/sys_func_registry.c/.h`
- relevant type/function ABI headers

Actions:

1. Add shared emission helpers for ERROR-tag tests, companion-lane tests,
   current-frame error adoption, and error-epilogue branches.
2. Route `raise`, postfix propagation, handler operands, fallible sys funcs,
   direct calls, indirect calls, methods, imports, and host adapters through
   those helpers.
3. Audit every typed/native call path: the companion error lane must dominate
   all reads/unboxing of the success lane.
4. Audit boxed paths: ERROR must be tested before property access, arithmetic,
   mutation, fixed-lane store, or success-only helper invocation.
5. Ensure every error epilogue runs current-frame cleanup in D5.1.4 order.
   A callee does not restore its caller's roots, number homes, scopes, or
   module state.
6. Preserve error identity. Do not replace an existing rich error with
   `ItemError`; allocate/enrich only at the origin or an explicit wrapper.
7. Remove any ordinary-error call to `lambda_recovery_frame_raise_fault()` or
   `lambda_recovery_frame_raise_local_fault()` found by P0.
8. Keep `Context::last_error` diagnostic-only. Add assertions/tests that
   clearing it cannot turn an ERROR completion into success.
9. Make unknown helper metadata fail closed as fallible; do not infer
   infallibility from a raw C return type.

Exit gate: nested synchronous Lambda calls return the same error through each
frame, each cleanup witness fires once, and no ordinary path reaches recovery.

### P3 — Decouple synchronous handlers from fault recovery

**Primary symbol:** `transpile_handler()`.

Actions:

1. Lower the ordinary operand first through the normal explicit completion
   ABI regardless of fault capability.
2. Keep the ERROR tag/lane branch local to the caller activation.
3. Scope/rename `transpile_local_fault_expression()` so its API accepts only a
   fault-capable procedure-handler operand and cannot be called for `raise`,
   propagation, JS throw, or generic error handling.
4. The local recovery frame protects only synchronous operand execution. End
   it before entering either handler arm so a fault in the body routes outward.
5. On ordinary success/error, pop the fault frame by normal return before
   selecting the arm. The jump is not involved in the ordinary edge.
6. Verify nesting restores `handler_error_reg`, `in_handler`, and outer handler
   state on every normal generated path.
7. Verify handler-body errors branch to the current function epilogue or an
   enclosing handler, never back to the same handler label.

Exit gate: synchronous `pn_call() ^ { ... }` passes for returned
`T | error`, raised `T^E`, and success; recovery instrumentation reports zero
jumps for those cases. Existing S7.11 local-fault tests remain isolated.

### P4 — Resume-safe `pn_call() ^ { ... }` ordinary errors

**Files:** `lambda/runtime/transpile-mir.cpp`, concurrency analysis in
`lambda/runtime/build_ast.cpp`, and async-frame planning/runtime helpers.

Actions:

1. Allocate an async state immediately after the possibly-suspending call
   using the existing `async_emit_invoke_resume_point()` machinery.
2. Keep the call completion boxed/rooted across the park. If the call has a
   native success lane, box/adopt it only after proving the companion error lane
   clean.
3. At the resume state, test the stored completion and branch to the existing
   error body, optional `~` body, or one-arm success continuation. Do not replay
   the call or arguments.
4. Store handler-local `^` in a rooted async slot while the body can suspend;
   nested handler entry saves and later restores the outer slot binding.
5. Clear the operand's handler/fault target before the body executes.
6. Route cancellation delivered to the call as an ordinary ERROR completion;
   it follows the same branch and structured task-scope cleanup.
7. Change `validate_handler_await_node()` to permit only the proven safe case:
   `AST_HANDLER_PROC_STATEMENT` whose operand is a procedure call lowered by
   the async state machine. Keep E221 for unsupported value-producing `pn`
   contexts.
8. Add MIR assertions that no recovery frame or jump-buffer address is spilled
   into async slots.

Exit gate: success, returned error, raised error, cancellation, and nested
handlers work before and after at least one real park, with exact-once call and
handler counters.

### P5 — Durable native-fault delivery for suspended handlers

**Files:**

- `lambda/runtime/concurrency.cpp/.h`
- `lambda/runtime/recovery_frame.c/.h`
- `lambda/runtime/transpile-mir.cpp`
- `lambda/runtime/lambda-stack.cpp/.h`

Actions:

1. Add the task-owned async fault-target descriptor from §4.4. It stores only
   task/frame state identifiers and rooted slot indices, never native stack
   addresses.
2. Emit register/clear operations around a suspending procedure-handler
   operand. Registration failure is an OOM/system fault, not a soft null.
3. Split the task poll recovery landing into:
   - deliver to nearest valid durable handler target; or
   - complete the task when no target exists.
4. Centralize abandoned-frame destruction and root/scope restoration. Do not
   duplicate frame teardown in the scheduler and emitter.
5. Validate the target through the current task's linked async-frame list and
   active state before routing. The descriptor is never exposed as a pointer
   across the scheduler boundary, so task ownership plus live-frame traversal
   is the generation check for this implementation.
6. Preserve the `LambdaFaultReason` and task-owned `LambdaFaultRecord` identity
   through materialization.
7. Clear the target before resume and prove a second fault in the body selects
   the next outer target or task boundary.
8. Keep all jumps same-thread and within one task poll. A fault never jumps
   into an earlier poll or another worker.
9. Add forced-GC tests after fault materialization and before handler resume to
   prove the fault carrier is rooted.

Exit gate: each closed S7.11 fault reason either resumes the nearest active
procedure handler exactly once or completes the task when none exists; no
ordinary error exercises this machinery.

### P6 — AST interpreter parity

**Files:**

- `lambda/runtime/interp.cpp/.hpp`
- `lambda/runtime/interp_plan.cpp`
- `test/lambda/interp_excluded.txt`

Actions:

1. Implement one `eval_handler()` for both handler AST node types.
2. Evaluate the operand once into a `Scratch`/planned side-root slot; never
   retain a GC-visible error only in a C++ local across body evaluation.
3. On error, save the enclosing current-error binding, expose the rooted error
   to `AST_NODE_CURRENT_ERROR`, evaluate the body, and restore the outer
   binding.
4. On statement success, return `ItemNull`; on expression success preserve the
   existing one-/two-arm value rules.
5. Respect `EvalSignal`: handler bodies may return, break, continue, or produce
   `ERROR_SKIP`, and sequencing stops immediately.
6. Ordinary interpreter calls return frame by frame. Interpreter recovery
   boundaries remain fault-only.
7. Add handler scripts to the interpreter differential set and remove their
   `interp_excluded.txt` entries only after exact JIT parity.

Exit gate: JIT/interpreter outputs and error identity match for all synchronous
handler cases; unsupported async interpreter cases fail explicitly, not via a
hidden JIT fallback.

### P7 — LambdaJS conformance audit

**Files:**

- `lambda/js/js_mir_completion.cpp`
- `lambda/js/js_mir_statement_lowering.cpp`
- `lambda/js/js_mir_function_class_lowering.cpp`
- `lambda/js/js_mir_iterator.cpp`
- `lambda/js/js_mir_entrypoints_require.cpp`
- `lambda/js/js_runtime_state.cpp/.hpp`
- Promise/module/callback lowering files

Actions:

1. Trace source `throw` and every runtime throw helper from `js_throw_value()`
   to the immediate generated catch/finally or function error epilogue.
2. Verify every fallible property, call, construct, iterator, coercion, module,
   eval, and callback helper returns an ERROR carrier and is guarded before
   success use.
3. Verify `finally` executes in its owning frame and may preserve or replace
   the incoming completion according to ECMAScript semantics.
4. Remove any pending-exception polling or plausible-success sentinel used as
   control flow. Diagnostic mirrors may remain non-authoritative.
5. Verify Promise rejection and async/generator throw are stored resume inputs,
   not pointers to old native catch frames.
6. Keep LambdaJS entrypoint recovery frames fault-only. Add a test that JS
   `throw` cannot increment native recovery-jump instrumentation.
7. Preserve primitive thrown values and object identity across three or more
   JS/generated/native frames and module boundaries.

Exit gate: LambdaJS exception, finally, async, generator, callback, and module
tests pass with zero ordinary recovery jumps and exact payload identity.

### P8 — Jube and hosted-language adapters

**Files:** `lambda/jube/`, language modules under `lambda/module/`, and host
adapter entrypoints.

Actions:

1. Extend callable metadata from a single `can_raise` bit where necessary to an
   explicit completion representation: infallible, merged Item, or companion
   error lane.
2. Make missing/unknown metadata fail closed.
3. Audit host-to-guest and guest-to-host adapters. A guest exception is caught
   inside the immediate foreign adapter, converted to a host completion, and
   returned normally.
4. Prevent guest exception machinery from crossing a Lambda-owned frame or
   reusing LambdaJS lexical exception state.
5. Audit module initialization and rollback: the owner frame performs rollback
   before returning the same failure outward.
6. Add cross-language identity and cleanup tests for Lambda→JS→Lambda and at
   least one non-JS guest path supported by the current baseline.

Exit gate: every catalog callable has checked completion metadata and every
guest failure crosses each host boundary by ordinary return.

### P9 — Mechanical enforcement

Actions:

1. Add a lint/CI jump allowlist covering `setjmp`, `longjmp`, SEH recovery,
   `LambdaRecoveryFrame`, and recovery-raise calls. A new site fails CI unless
   classified as S7.11 or test/process containment.
2. Keep recovery origin APIs typed as `LambdaFaultReason`. No overload accepts
   `Item`, JS value, `LambdaErrorCode`, or arbitrary integer.
3. Add catalog completeness validation at generation/build time.
4. Keep companion-lane checking in the shared MIR materialization helpers and
   catalog gates; a separate debug verifier is not needed for the current
   generated shapes because every native-to-Item boundary goes through those
   helpers.
5. Instrument recovery jumps in tests and assert zero for ordinary Lambda
   errors, JS exceptions, guest errors, cancellation, and Promise rejection.
6. Assert no async slot is marked as containing a recovery frame, `jmp_buf`, or
   native frame address.

Exit gate: the architecture is enforced mechanically, not only documented.

### P10 — Corpus migration, full gates, and documentation closeout

Actions:

1. Convert tests currently blocked only by E221 to the now-supported existing
   `pn_call() ^ { ... }` behavior; do not introduce new syntax.
2. Add a `.txt` expected result for every new `.ls` test.
3. Re-run the original 11-failure table. Every row must be fixed, explicitly
   reclassified as unrelated with evidence, or left as a visible failure; no
   harness masking is allowed.
4. Update `doc/dev/lambda/LR_10_Error_Handling.md`, LambdaJS runtime docs, Jube
   docs, and implementation footnotes with landed status and exact gaps.
5. Mark phases complete in this file only after their exit gates pass.

Exit gate: baseline, concurrency, interpreter, JS, guest, GC-rooting, fault,
and release-size gates pass; docs match the implementation.

---

## 6. Test matrix

### 6.1 Lambda handler behavior

| Case | Expected result |
|---|---|
| direct `pn` success | handler skipped; following statement runs |
| direct `pn` returns `T | error` | handler runs once with same rich error |
| direct `pn` raises `T^E` | handler runs once; E228 satisfied |
| system `pn` error | same ordinary returned completion path |
| imported/indirect/method `pn` | same behavior after static `pn` validation |
| nested handlers | innermost `^` wins; outer binding restored |
| handler body raises | fresh error propagates outward; no self-catch |
| handler body returns/breaks/continues | normal procedural control signal |
| success value from `pn` | discarded in statement mode |
| two-arm `pn` handler success | `~` body runs once with the procedure result |
| `pn` handler in value binding | compile error retained |
| ignored raised `pn` call | E228 retained |

### 6.2 Suspension

| Case | Expected result |
|---|---|
| error before first park | handler runs once |
| error after one park | handler runs once after resume |
| error after multiple parks | handler runs once; arguments/call not replayed |
| success after park | handler skipped |
| cancellation while parked | handler receives cancellation completion |
| handler body itself parks | current `^` remains rooted and stable |
| nested suspended handlers | nearest active handler selected |
| outer handler after inner body error | outer handler receives fresh error |

### 6.3 Native faults

For each closed `LambdaFaultReason`, test synchronous, suspended, nested, and
no-handler cases where the platform can inject the reason safely.

Assertions:

- native recovery jump counter increments only for the injected fault;
- ordinary error counters remain zero;
- nearest valid procedural handler or task boundary receives one fault;
- abandoned async frames unregister roots and run permitted runtime teardown;
- the handler body is outside the consumed fault target;
- cross-thread/stale target delivery is rejected.

### 6.4 Cross-language and cleanup

- Lambda calls JS which throws through native helpers; every JS `finally` and
  Lambda frame cleanup witness runs once.
- JS calls Lambda which raises; the adapter returns a JS abrupt completion
  without native recovery.
- Module initialization fails after acquiring transactional state; rollback
  occurs in the owner frame before return.
- Callback and Promise rejection paths preserve payload identity.
- Forced GC at every carrier handoff retains the error and its `source` chain.

---

## 7. Validation commands

Run targeted gates during each phase, then the broad gates. Exact GTest filters
should be added with the new test suites rather than embedding unstable names
here.

```bash
make generate-grammar
make build-test
./test/test_lambda_errors_gtest.exe
./test/test_lambda_proc_gtest.exe
./test/test_lambda_concurrency_gtest.exe
make test-lambda-interp
make test-gc-rooting
make test-lambda-baseline
make test-lambda-full
```

For parser/archive or performance measurements, use only a release build:

```bash
make release
```

Then compare `lambda/tree-sitter-lambda/libtree-sitter-lambda.a` against the P0
release baseline. The expected parser delta is zero or noise because this plan
adds no grammar rule.

Always finish a documentation/code slice with:

```bash
git diff --check
```

Do not manually edit generated `parser.c` or `ts-enum.h`; regenerate them from
`grammar.js` only when the generator inputs actually change.

---

## 8. Risk register

| Risk | Prevention / proof |
|---|---|
| ordinary error accidentally selects recovery | typed fault API, jump instrumentation, negative tests |
| success lane read before companion check | shared guard helper plus MIR dominance verifier |
| handler call replay after resume | state-id and exact-once counters around arguments/callee |
| dead `jmp_buf` retained across park | no native addresses in async slots; runtime assertions |
| task fault resumes stale frame | generation token, active bit, ownership validation |
| fault carrier collected before resume | task-owned fault record plus rooted async error slot |
| handler catches its own body error/fault | clear target before arm entry; nested tests |
| outer `^` lost after nested handler | explicit save/restore in MIR and interpreter |
| abandoned async roots/scopes leak | centralized frame teardown and watermark tests |
| JS `finally` skipped or duplicated | completion identity plus side-effect count tests |
| helper metadata claims false infallibility | fail-closed catalog and generated validation |
| parser grows or conflicts return | no grammar change; release archive size/CST gate |
| test harness hides runtime bug | preserve failures; never alter harness expectations to mask them |

---

## 9. Completion criteria

The rework is complete only when all of the following are true:

1. Every ordinary Lambda error, LambdaJS exception, guest failure, rejection,
   and cancellation returns through every active frame.
2. No ordinary failure invokes `setjmp`/`longjmp`, C++ exceptions, SEH unwind,
   or `LambdaRecoveryFrame`.
3. `pn_call() ^ { ... }` works synchronously and after suspension,
   evaluates its operand once, and preserves handler-local `^` correctly.
4. Native faults remain the only users of non-local recovery and can reach a
   live or durable procedural handler without retaining a dead native frame.
5. Every fallible helper has explicit, validated completion metadata.
6. Lambda MIR, the AST interpreter, LambdaJS, and hosted-language adapters pass
   identity, cleanup, GC-rooting, async, module, and fault-separation tests.
7. The original 11 failures have evidence-backed dispositions and no failure
   is hidden by test-runner changes.
8. Formal, user, runtime, LambdaJS, Jube, and implementation-plan documents all
   describe the landed mechanism consistently.

The implementation is complete for the scoped runtime paths. Appendix A of the
formal specifications records S7.6.7 and D8.4.3v2 as landed; unrelated open
items in those appendices remain outside this rework.

## 10. Landed implementation ledger

| Phase | Result |
|---|---|
| P1 | Existing handler grammar and AST node shape preserved; statement `pn` intent and async fault states are classified in the AST build pass. |
| P2–P3 | Ordinary ERROR/companion completions use local generated checks; synchronous procedure handlers use a fault-only local recovery frame that ends before either body. |
| P4 | `pn_call() ^ { ... }` survives park/resume with rooted completion, exact-once operand evaluation, nested current-error scope, and statement-only possibly-suspending `pn` handlers. |
| P5 | Task polls materialize S7.11 faults into rooted async-frame state, cancel abandoned child frames/scopes, and re-enter the nearest durable handler without retaining native recovery state. Linked task ownership and active target state reject stale delivery; no descriptor pointer crosses the scheduler boundary. |
| P6 | AST interpreter handler parity and explicit recursion-budget completions are landed; supported error fixtures are in the interpreted subset. |
| P7–P8 | LambdaJS, Jube, and hosted entry-point recovery boundaries were audited and remain fault-only; ordinary guest/Lambda failures use explicit completion lanes. |
| P9 | `make check-error-recovery` enforces the native recovery allowlist and rejects ordinary recovery call sites. JS callable/exception catalog gates remain green. |
| P10 | Handler/concurrency fixtures, native-fault-after-wait coverage, interpreter differential coverage, and formal/runtime docs were updated. |

The implementation intentionally preserves `pn_call() ^ { ... }`; it does not
introduce `pn_call() { ... }`, a new grammar rule, or a second procedure-handler
spelling. All rulings above are governed by D1.4v3, D6.3.3, D8.4.3v2,
S7.6.1v4, S7.6.7v3, S7.11, and REH-D1–REH-D14.

## 11. Validation record

Passed: `make build`, `make build-test`, `make check-error-recovery`,
`./test/test_lambda_errors_gtest.exe` (104), `./test/test_lambda_proc_gtest.exe`
(6), the full Lambda suite (726/726), the focused former-11-failure Lambda set
(17/17), focused concurrency fault and GC tests, the interpreter handler/GC
probes, the full interpreter differential suite (352/352), the JS MIR-emission
suite (21/21), the JS optimization suite (19/19), and
`make test-jube-node-error-lane`.

Jube module-integrity, loader-negative, and node-error lanes also passed. The
debug-build Jube language-dispatch gate remains a pre-existing tooling
mismatch: the binary writes its normal debug banner to stderr despite
`--no-log`; the dispatch harness was not changed.

The current full JS unit sweep is 350/351: the sole failure is the unrelated
`lib_tabulator` scroll-row expectation mismatch. The full concurrency runtime
binary is 14/16; its two failures are the unrelated runtime-global chart/PDF
module-state name-allocation failures (`module-key-link: name allocation failed
for property key 4`). Neither result was hidden or altered by the rework; the
test harness remains unchanged.

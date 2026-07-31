# Lambda execution recovery: current non-local-jump inventory and redesign ledger

**Status:** IMPLEMENTED ON POSIX — ER-S0 through ER-S6 are implemented and
verified. ER-S7's precise-root/POSIX gate is complete; the matching Windows SEH
integration test remains an external Windows-runner requirement. The live
inventory was rechecked 2026-07-31. The shared execution target has been
replaced by a TLS LIFO frame at every former target site; resource faults use
the static channel while arbitrary event-loop memory faults fail-stop.

**Date:** 2026-07-17

**Companion documents:**

| Document | Relationship |
|---|---|
| `vibe/Lambda_Design_Stack_Rooting.md` | RH8 requires exact watermark restoration across every non-local unwind |
| `vibe/Lambda_Stack_Safety.md` | Defines the signal-based stack-overflow mechanism, now routed through the recovery-frame target |
| `vibe/Lambda_Design_Stack_Frame.md` | Defines root/number frame lifetime and normal epilogue invariants that a jump bypasses |
| `vibe/Lambda_Design_Type_Enforcement.md` | Defines C14's unchecked system-fault channel and the `pn` `^err` requirement |
| `vibe/Lambda_Impl_Type_Enforce.md` | Owns Phase 5's implementation sequencing and standing gates |

## 1. Purpose and scope

Lambda currently uses several independent `setjmp`/`longjmp` and
`sigsetjmp`/`siglongjmp` mechanisms for stack overflow, timeouts, batch crash
containment, MIR errors, and event-loop crash recovery. They were added for
different purposes and do not share one checkpoint, nesting, cleanup, or state
restoration contract.

This document records every current use in the Lambda/LambdaJS execution
runtime, distinguishes real non-local recovery from local library/error uses,
and preserves the redesign requirements for a future implementation session.

Why this matters to precise rooting:

```text
normal return/error branch
    generated epilogue restores root and number watermarks
    C++ destructors restore helper RootFrames

longjmp/siglongjmp
    generated epilogues are skipped
    C++ destructors are skipped
    native cleanup code between origin and landing point is skipped
```

Every non-local jump that can cross generated or native root frames must
therefore restore all affected runtime watermarks explicitly at its landing
point. Passing normal tests while relying on later context destruction is not
an adequate recovery contract.

## 2. Executive inventory

| Mechanism | Jump origin | Landing point(s) | Current side-stack handling |
|---|---|---|---|
| Stack overflow, Unix | `runtime/lambda-stack.cpp` `siglongjmp(frame->jump_buffer, 1)` | `runtime/runner.cpp`; `runtime/transpile-mir.cpp`; `js/js_mir_entrypoints_require.cpp`; Jube guest entry | selected armed TLS frame restores the exact shared checkpoint, then is popped before reporting; transaction barriers win over inner local frames |
| Stack overflow, Windows | `runtime/lambda-stack.cpp` `longjmp(frame->jump_buffer, 1)` after `_resetstkoflw` | Windows branches of the same execution-entry families | same frame selection and checkpoint contract; Windows runtime gate remains pending |
| Simple file-batch timeout | `main.cpp:1371` | `main.cpp:3898` | no local snapshot/restore; later runtime reset |
| Test262 hot-batch timeout | `main.cpp:1371` | `main.cpp:4235` | common per-test snapshot/restore |
| Test262 hot-batch crash | `main.cpp:1360` | `main.cpp:4209` | common per-test snapshot/restore; interrupted resources intentionally leak until cleanup/reset |
| Test262 MIR error | `main.cpp:1347` | `main.cpp:4234`, `main.cpp:4263` | common per-test snapshot/restore |
| Production JS event loop | active `LambdaRecoveryFrame` for C14 faults | owning local/task/execution frame | no event-loop SIGSEGV target remains; arbitrary memory faults fail-stop |
| Conservative-GC register flush | retired | none | precise rooting must not restore or depend on native-stack scanning |
| MinGW intrinsic shim | `main.cpp:278` calls `setjmp(env)` | caller owned by the Windows/MIR ABI | compatibility shim, not a Lambda recovery policy |
| PNG/image errors | `lib/image.c`, including explicit `longjmp` at line 463 | local decode/encode `setjmp` sites | library-local; does not target Lambda recovery state |

## 3. Stack-overflow recovery

### 3.1 TLS LIFO recovery target (ER-S2/ER-S3 landed)

`lambda/runtime/recovery_frame.c` now owns `lambda_recovery_frame_tls_top`. Each
frame carries a platform buffer, `signal_armed`, an enum-sized
`signal_fault_reason`, a precise checkpoint, and its previous frame. The POSIX
handler reads that TLS link directly, chooses the nearest eligible armed frame
unless an enclosing transaction barrier exists, which takes priority over every
inner local handler. It sets `STACK_OVERFLOW`, and
`siglongjmp`s to its buffer. It neither logs nor allocates; an unarmed or
non-stack fault follows the fail-stop path. The Windows SEH branch uses the same
selection after `_resetstkoflw`.

ER-S3 adds a direct MIR `sigsetjmp` (POSIX) / `setjmp` (Windows) checkpoint
around a non-suspending `pn` `let value^err = expression` RHS. The checkpoint is
not a helper that has returned: the generated JIT activation owns the platform
call and uses the frame's jump-buffer offset. On landing it restores the exact
checkpoint, copies the embedded static fault record to TLS fallback storage,
then exposes that `ItemError` to the existing evaluator error state before the
frame is released. A may-suspend RHS keeps ordinary `^err` value splitting but
does not receive a native target; its C14 fault belongs to its task boundary.

The former single `_lambda_recovery_point`/Boolean target was the pre-ER-S2
design defect; no production source still references it.

### 3.2 Normal Lambda runner landing point

`lambda/runtime/runner.cpp`:

1. acquires and pushes an execution frame before the direct `setjmp`;
2. arms only around `main_func(context)`;
3. after a jump, restores the exact checkpoint and pops the frame before
   reporting the current `ItemError` stack-overflow result; and
4. ends the frame on every normal return, restoring the outer target.

This is the most complete current example of the RH8 behavior.

### 3.3 Cached Lambda MIR landing point

`lambda/runtime/transpile-mir.cpp` uses the same frame pattern for cached MIR
execution: push, direct checkpoint, arm generated execution, exact restore on a
landing, then pop before converting the result to `ItemError`.

### 3.4 Direct LambdaJS MIR landing point

`lambda/js/js_mir_entrypoints_require.cpp` pushes a direct-JS execution frame
around `js_main`. Its landing restores then pops the frame before throwing the
JS `RangeError`; normal nested return resumes the still-armed outer frame.

### 3.5 Hosted Jube guest landing points

Both hosted-Jube entry forms push an execution-plus-transaction frame with the
guest's active context. A landing restores guest-side watermarks, publishes the
static fault result, then poisons the guest's frame-runtime and scalar-home
slots before returning to the host. Subsequent guest calls reject the poisoned
activation; normal guest completion still restores the prior nested activation.

### 3.6 Retired shared-target defect

Before ER-S2, the runner, cached MIR, direct JS, and hosted Jube entries
overwrote one TLS jump buffer plus Boolean flag. That non-nestable target is now
removed. Batch containment remains separate and test-only; the production event
loop no longer owns any SIGSEGV recovery target.

## 4. Batch timeout recovery

### 4.1 Shared timeout origin

`lambda/main.cpp:1331–1373` defines:

```text
static sigjmp_buf batch_timeout_jmp
static volatile sig_atomic_t batch_timeout_active
```

`batch_alarm_handler()` handles `SIGALRM` and, while active, executes:

```text
siglongjmp(batch_timeout_jmp, 1)
```

### 4.2 Simple file-batch landing point

`lambda/main.cpp:3898` establishes `batch_timeout_jmp` around
`run_script_file()`. A timeout jumps over the runner, generated epilogues, and
native cleanup and returns result code `124`.

There is no timeout-local `LambdaSideStackSnapshot`. The command subsequently
calls `runtime_reset_heap()`, so it uses teardown/reset as containment rather
than structured restoration. It can also bypass the stack-overflow runner's
disarm assignment.

### 4.3 Test262 hot-batch landing point

`lambda/main.cpp:4235` establishes the same timeout buffer inside the Test262
per-test crash boundary. Before establishing any per-test jump target,
`main.cpp:4199` captures `batch_side_stack_snapshot`. All timeout, crash,
MIR-error, and normal paths converge on:

```text
lambda_side_stack_restore(&batch_context, batch_side_stack_snapshot)
```

at `main.cpp:4308`.

This restores root and number watermarks. Timeout/crash paths may additionally
destroy and recreate the heap/context because interrupted MIR and native
resources did not run their normal cleanup.

## 5. Test262 crash recovery

`lambda/main.cpp:1354–1364` defines `batch_crash_jmp` and a signal handler that
executes:

```text
siglongjmp(batch_crash_jmp, signal_number)
```

The Test262 batch loop installs this handler for:

```text
SIGSEGV
SIGBUS
SIGABRT
SIGTRAP
```

The landing point is `lambda/main.cpp:4209`. The handler is active across the
batch loop, including between-test cleanup. A crash inside a test becomes
`128 + signal`; a crash between tests exits the batch so the harness can retry
remaining tests separately.

The per-test side-stack snapshot is restored through the common path. Other
interrupted resources are not unwound. Current comments estimate roughly 55 MB
of leaked MIR/AST/temporary state per recovered crash, so the batch enforces
crash-count and RSS limits and may destroy/recreate its heap/context.

This is containment for a test worker, not a general language exception model.
The redesign should keep arbitrary-crash containment isolated from normal
runtime recovery.

## 6. Test262 MIR-error recovery

`lambda/main.cpp:1335–1350` defines `mir_error_jmp` and installs
`batch_mir_error_handler()` as MIR's error callback. While
`mir_error_active` is true, an internal MIR error executes:

```text
longjmp(mir_error_jmp, 1)
```

Landing points:

- `lambda/main.cpp:4234` when the batch also has a timeout boundary;
- `lambda/main.cpp:4263` when no timeout is configured.

Both paths converge on the common per-test side-stack restore. They still skip
normal MIR/native cleanup between the error origin and landing point.

## 7. Production JS event-loop crash policy

ER-S6 removed the event-loop-local `event_loop_jmpbuf`, SIGSEGV handler, and
post-signal continuation path from `lambda/js/js_event_loop.cpp`. A C14 resource
fault raised while draining work reaches the active local, task, or execution
`LambdaRecoveryFrame`; arbitrary memory faults remain fail-stop and are never
recast as language `ItemError` values. Test262 keeps its independent, test-only
batch crash/timeout containment policy.

## 8. `setjmp` uses that are not Lambda execution recovery

### 8.1 Retired conservative GC register flush

The former `setjmp(regs)` register-spill trick belonged solely to conservative
native-stack scanning. It is retired with that scanning mode and is expressly
not a recovery mechanism or a fallback available to Phase 5. Every landing
must rely on exact side-root state restored from its checkpoint.

### 8.2 Windows intrinsic compatibility shim

`lambda/main.cpp:274–279` defines a MinGW implementation of
`__intrinsic_setjmpex()` that delegates to ordinary `setjmp`. The caller owns
the ABI-level jump behavior; this shim is not one of Lambda's execution
recovery policies.

### 8.3 Image decoding and libpng

`lib/image.c` contains local image-error recovery:

- libpng landing points through `setjmp(png_jmpbuf(png_ptr))` at lines 100,
  196, and 720;
- a custom `PngErrorContext` landing point at line 492;
- `png_lenient_error_handler()` calls `longjmp(ctx->jmpbuf, 1)` at line 463.

These jumps target buffers owned by the same image decode/encode operation and
do not use Lambda's execution recovery variables. They remain subject to C
resource-lifetime rules but should not be merged blindly with user-code stack
overflow, timeout, or batch-crash policy.

## 9. Current restoration matrix

| Path | Root top | Number top | Recovery flag/target | Other cleanup |
|---|---|---|---|---|
| Normal Lambda stack overflow | restored | restored | frame popped before report; outer target resumes | converts to `ItemError` |
| Cached MIR stack overflow | restored | restored | frame popped before report; outer target resumes | converts to `ItemError` |
| Direct LambdaJS stack overflow | restored | restored | frame popped before report; outer target resumes | throws JS `RangeError`, continues cleanup |
| Hosted Jube stack overflow | restored | restored | guest frame popped; outer target resumes | returns guest `ItemError` |
| Simple batch timeout | **no local restore** | **no local restore** | separate batch target | runtime reset afterward |
| Test262 hot-batch timeout | restored by common test path | restored | separate batch flags reset | interrupted context may be recreated |
| Test262 batch crash | restored by common test path | restored | batch crash flag re-enabled | intentional leak containment + possible context reset |
| Test262 MIR error | restored by common test path | restored | MIR flag reset | skips intervening MIR/native cleanup |
| Production JS event loop | n/a — no event-loop jump target | n/a — no event-loop jump target | active C14 frame only | arbitrary SIGSEGV fails-stop; C14 lands at its owner |

The matrix covers only root/number watermarks and obvious recovery flags.
Future inventory must include any JS argument-stack mark, active module/eval
context, active MIR context, scheduler/callback state, current `Context*`, and
other dynamic state that can change between checkpoint and jump.

## 10. Structural design problems

### ER1 — Multiple unrelated recovery mechanisms

Stack overflow, timeout, batch crash, MIR error, and event-loop crash each
define their own buffer, active flag, handler, result convention, and cleanup.
There is no common recovery record or required state snapshot.

### ER2 — Execution targets are now nestable; separate containment remains

ER-S2 replaced the one-slot execution target with a per-thread explicit frame
LIFO. Test-only batch containment remains separate; ER-S6 removed the
production event-loop buffer, so arbitrary faults are not language-catchable.

### ER3 — Watermark restoration is incomplete

The normal Lambda runner, cached MIR, direct JS, hosted guest, task poll, and
Test262 common paths use their documented recovery restoration. Simple batch
timeout remains reset-based, and no existing checkpoint captures every
auxiliary watermark required by §11.9.

### ER4 — Handler ownership conflicts

The stack-overflow system and the Test262 worker protocol both use signal
containment in their own scopes. ER-S6 removed the production event-loop
handler; the remaining test-worker behavior must stay isolated from language
recovery targets.

### ER5 — Non-local jumps skip ownership cleanup

C++ destructors, generated epilogues, MIR teardown, libuv cleanup, and other
native release paths are bypassed. Some paths restore watermarks; none can
retroactively execute arbitrary skipped destructors.

### ER6 — Signal-handler safety is inconsistent

Some handlers log, change handlers, or use ordinary `longjmp`. The redesign
must define the minimal async-signal-safe work allowed before jumping to a
safe landing point.

### ER7 — Recovery result semantics differ

Stack overflow becomes `ItemError` or JS `RangeError`; timeout becomes `124`;
crash becomes `128 + signal`; and MIR errors become result `1`. These boundary
conventions require typed internal reasons so cleanup does not infer behavior
from ad-hoc integers.

### ER8 — Test containment and production recovery are mixed

Recovering from arbitrary SIGSEGV/SIGABRT is useful for a Test262 worker but is
not generally safe in a long-lived production process. The redesign must keep
test-worker containment separate from recoverable language/runtime failures.

## 11. Future redesign requirements

The future design should introduce one explicit, nestable recovery-frame
contract. Names below are placeholders, not an approved API.

### 11.1 Nestable recovery frames

Each thread maintains a linked/LIFO stack of recovery frames:

```text
LambdaRecoveryFrame
    previous frame
    jump buffer appropriate to the platform
    recovery kind/capabilities
    owning Context*
    LambdaSideStackSnapshot
    auxiliary runtime watermarks
    previous armed/signal state
    typed recovery reason and payload
```

Push saves the previous frame and all required state. Structured pop restores
the previous recovery target. A non-local landing restores the saved runtime
state before allocation, GC, error construction, callbacks, or continued
execution.

### 11.2 Typed recovery reasons

At minimum distinguish:

```text
STACK_OVERFLOW
TIMEOUT
MIR_ERROR
TEST_PROCESS_CRASH
EVENT_LOOP_FAULT        // preferably eliminated or test-only
```

The landing policy maps the typed reason to `ItemError`, JS exception, CLI
exit status, batch protocol, or process termination.

### 11.3 Central state snapshot/restore

One helper owns the RH8 checkpoint. It includes root and number tops and an
audited list of every auxiliary dynamic watermark. Recovery sites may add
subsystem-specific cleanup, but they cannot omit the common restore.

### 11.4 Signal-handler discipline

Signal/SEH handlers perform only minimal classification, store a typed reason
in preallocated recovery state, and jump. Logging, exception creation, heap
reset, MIR cleanup, and signal-handler restoration happen at the landing point
where ordinary runtime operations are safe.

### 11.5 Explicit containment policy

Recoverable faults (stack overflow, controlled timeout, MIR error) use runtime
recovery frames. Arbitrary memory faults should normally terminate the process;
if Test262 needs containment, keep it at the worker-process boundary with an
explicit test-only capability.

### 11.6 No reliance on destructors across jumps

Any resource that must survive/recover across a jump is represented in the
checkpoint or owned outside the jumped-over region. RAII remains valid for
structured exits but is not cited as cleanup for a `longjmp` path.

### 11.7 Decided frame ABI and target selection

Phase 5 adopts a TLS LIFO of explicit frames. The current
`LambdaRecoveryCheckpoint` is retained as the side-stack portion of a larger
frame; it is not itself a jump target.

```text
LambdaRecoveryFrame
    previous                       // TLS LIFO link; never process-global
    discarded_inner                // retired after a transaction-priority landing
    Context* context               // exact EvalContext owner
    RecoverySnapshot snapshot      // §11.9, captured before protected work
    FaultRecord fault              // embedded; no allocation on fault
    jump buffer                    // sigjmp_buf on POSIX, jmp_buf on Windows
    capability mask                // local fault catch, execution boundary,
                                   // transaction barrier, batch-test-only
    state                          // prepared, armed, landed, disarmed
```

`lambda_recovery_tls_top` replaces `_lambda_recovery_point` and
`_lambda_recovery_armed`. Pushing links the old top; a normal pop proves it is
still the top and restores `previous`. A fault selects the nearest frame that
accepts that fault, except that an enclosing transaction barrier takes priority:
the landing first makes that barrier the TLS top, restores its snapshot, and
retires the abandoned inner chain. It then unlinks the selected frame before
executing a user handler or any allocating/reporting code. A second fault in a
handler therefore targets the outer frame, never a stale or re-entered buffer.

`setjmp` is special: its dynamic extent is the function containing the call.
Push/pop, snapshot, and fault publication may be C/C++ helpers, but no helper
may call `setjmp` and return before its buffer is jumped to. The runner, JS,
Jube, and each MIR-emitted local handler must establish the checkpoint in their
own native/JIT activation. The MIR emitter imports the platform checkpoint
primitive directly; it must not simulate this with a returned helper or an
RAII-only wrapper.

### 11.8 C14 fault semantics: local `pn` `^err` then global boundary

The existing `let value^err = expression` syntax remains the only local source
surface. It always keeps its existing ordinary destructuring result for a
returned/raised `ItemError`, including when the RHS suspends. In a `pn`, MIR
adds a single-use local-fault frame only around an RHS proven not to suspend.
If an unchecked system fault lands in that native-safe region instead, it skips
the protected expression and binds:

```text
value = null
err   = ItemError carrying the pre-reserved FaultRecord
```

The next statement is executed normally. This is the precise meaning of a
`pn` `^err` boundary. The handler can inspect `err.code`, `err.kind`, and the
static diagnostic message; it cannot resume the abandoned expression. An `fn`
may continue to destructure ordinary returned/raised errors, but does not
install a system-fault landing point: faults pass transparently through `fn`
frames. A RHS that may await, yield, or return through a callback remains legal
for ordinary `^err` destructuring, but it receives no local C14 frame: its
native `jmp_buf` would outlive the poll. A C14 fault there is owned by the task
execution boundary described in §11.11.

If no eligible local `pn` boundary remains, the execution-boundary frame owns
the fault. The CLI runner terminates the current execution with its report and
non-zero status; a hosted Runtime receives an immutable fault descriptor through
its configured global callback. The callback runs only after restoration, may
choose abort/report policy, and cannot resume at the origin or convert the
fault into a normal typed return. This is the only global handler in v1; no new
language-level catch syntax is introduced.

This control transfer is unchecked and never contributes `error` to a function
type. A caught fault is observable through the existing `ItemError` marker only
at the selected `^err` boundary; it is not a normal call-result ABI or an
implicit `T | error` value.

### 11.9 Exact restoration before fault observation

`RecoverySnapshot` is captured before entering protected work and restored
before the landing branch stores `ItemError`, runs a handler, logs, allocates,
drains tasks, or permits GC. The initial mandatory fields are:

1. the `Context*` identity and the TLS active-evaluator owner;
2. `side_root_top` and `side_number_top` through the existing exact
   `LambdaRecoveryCheckpoint` API, including no stale RootFrame slots;
3. active MIR return-lane/scalar-home extent and every JIT/debug activation
   watermark that can be changed below the frame;
4. LambdaJS argument-frame, CommonJS/module, and eval-source stack depths;
5. scheduler current-task/async-frame cursor and callback-dispatch state; and
6. hosted-guest/Jube activation ownership and module-initialisation state.

Each item has one named capture/restore pair and a test that forces collection
immediately after landing. A field absent from the snapshot is not allowed to
be repaired later by context destruction. C++ `RootFrame` destructors skipped
by the jump are harmless only because their slots fall above the restored exact
watermark; there is no conservative native-stack scan to mask a missed field.

The existing checkpoint currently restores only the first part of item 2 and
rejects an `EvalContext` owner change. Phase 5 extends it rather than adding
per-entrypoint ad-hoc snapshots.

### 11.10 Fault record and OOM rule

Fault delivery cannot allocate. Every recovery frame embeds a pre-initialized
`FaultRecord`; each active Context also has a pre-reserved global fallback. A
record contains a reason enum, an optional fixed prior-error code, static source
identity, and a non-owning, prebuilt `LambdaError`/`ItemError` view. Its storage
is explicitly marked static so `err_free` never releases it. The supported v1
reasons are:

```text
STACK_OVERFLOW
SIDE_STACK_EXHAUSTION          // RootFrame or number-home reservation
OUT_OF_MEMORY
EQUALITY_DEPTH_EXHAUSTION
RUNTIME_BOUNDARY_DEFECT        // compiler-inserted fail-closed guard only
```

The first four always use fixed code/message/kind data; no stack trace, string
formatting, map construction, or heap allocation is attempted on the origin or
landing path. `err.message` is backed by a pre-reserved fault string so a
`pn` handler can inspect it without allocating. Rich diagnostic objects remain
for the two typed channels only.

If construction of an ordinary rich error fails, the primary error is discarded
and the runtime raises `OUT_OF_MEMORY` with its `prior_error_code` populated.
It must never recursively attempt to allocate another rich error. Optional
caches and best-effort diagnostics may still decline work locally; any required
runtime allocation that cannot preserve a sound execution state raises the
unchecked OOM fault. Existing `set_runtime_error_no_trace()` is not valid for
this path because `err_create()` allocates.

### 11.11 Re-entry, modules, async, workers, and hosted guests

- **Nested eval/import/module execution:** every execution entry pushes an
  execution-boundary frame and restores the previous target on normal return.
  Module/eval initialisation is a transaction barrier: it first restores and
  marks the partial module/eval failed or discardable, then forwards the same
  fault to the next eligible target. A local `^err` must never resume through a
  half-initialized module.
- **Callbacks and event-loop drain:** a callback runs under the runtime frame
  active for that drain. Arbitrary `SIGSEGV`/`SIGBUS` is not a C14 fault and is
  fail-stop in production; Test262 may retain process/batch containment behind
  a test-only capability. The current event-loop crash guard must not intercept
  a stack-overflow handler or continue a corrupted production process.
- **Async tasks:** recovery frames are native-stack local and never survive a
  yield. Each task poll establishes a task execution boundary; a fault either
  lands in a non-suspending local `pn` boundary or completes that task with the
  static fault result for scheduler/global handling. It cannot jump into a
  resumed activation from an earlier poll.
- **Workers:** the frame stack and fallback record are TLS. No `longjmp` crosses
  a worker boundary; a worker publishes only an already-materialized fault
  result after its own landing and restoration.
- **Hosted/Jube guests:** guest entry is a transaction barrier. It restores the
  guest's side-stack/TLS activation and releases or poisons the guest execution
  before forwarding to a guest-local handler or its host execution boundary.
  It cannot leave `jube_active_guest_execution` or a scalar result home pointing
  into abandoned guest state.

### 11.12 Platform and signal rules

On POSIX, stack faults use `sigaction(..., SA_ONSTACK)` plus
`sigsetjmp`/`siglongjmp`; a handler only classifies an eligible stack fault,
stores enum-sized state in the selected preallocated frame, and jumps. It does
not log, allocate, lock, alter handlers, construct an error, or call non
async-signal-safe helpers. An unarmed or non-stack signal takes the fail-stop
path. Windows uses the matching TLS frame selection through its SEH/`longjmp`
bridge after `_resetstkoflw`; its integration test is required, not inferred
from POSIX behavior. Timeouts, MIR errors, and Test262 crashes retain their
separate boundary policies but use the same snapshot/restore primitive; they
are not promoted to language-catchable system faults.

## 12. Suggested future implementation sequence

**Implementation status (2026-07-31):** ER-S0 through ER-S6 are complete on
POSIX. ER-S7's precise-root and POSIX recovery gates are complete; the Windows
SEH path shares the frame ABI but still requires a Windows integration run.
`LambdaRecoveryFrame` is a separate TLS-LIFO module with an embedded
`FaultRecord`, platform jump buffer, and exact side-stack/MIR-scalar checkpoint.
Runner, cached MIR, direct JS MIR, and hosted-Jube entrypoints now establish
their own checkpoint and restore the outer frame on normal nested return. The
MIR direct-local lowering now adds a frame only when the protected `^err` RHS
cannot suspend; ordinary asynchronous error destructuring remains a value-lane
operation. ER-S4 now converts stack/RootFrame/OOM failures to static origins;
ER-S5 adds task-poll and module transaction boundaries; and ER-S6 removes the
production event-loop crash continuation guard.

1. **ER-S0: lock the fault contract.** Add tests for the pre-reserved fault
   record and make the ordinary-error allocation-failure rule testable before
   routing any allocation failure into it.
2. **ER-S1: land the TLS frame ABI and exact common snapshot.** Push/pop and
   non-local landing are tested with nested native frames, forced GC, RootFrame
   and number-home exhaustion.
3. **ER-S2: migrate all execution entries.** Runner, cached MIR, direct JS,
   Jube, eval/import boundaries, and task polls use the frame top. A normal
   nested return proves the outer target remains armed.
4. **ER-S3: complete.** MIR emits the real checkpoint only for a
   non-suspending RHS; ordinary `^err` destructuring remains continuation-safe
   when the RHS may suspend, and the native root-fault test proves local-then-
   outer target selection.
5. **ER-S4: complete.** Stack and RootFrame origins now transfer static C14
   records; rich-error allocation failure becomes static OOM. Equality-depth
   takes the static path for a live procedural local handler while preserving
   the established functional returned-error flow.
6. **ER-S5: complete.** Each async task poll owns a short-lived execution
   frame and a task-owned static result; imported-module initialization resets
   partial module slabs before forwarding; hosted guests poison abandoned
   runtime/scalar slots. Batch timeout/MIR protocols are unchanged.
7. **ER-S6: complete.** The production event loop no longer replaces SIGSEGV
   handling or continues after arbitrary memory corruption. Test262 retains
   its separate test-only containment.
8. **ER-S7: POSIX and precise-root gate complete.** Shared frame tests cover
   exact recovery watermarks and native RootFrame faults, and POSIX baselines
   pass. The Windows SEH integration execution remains required on Windows;
   it is not inferred from these POSIX results.

## 13. Acceptance gates for the redesign

- every non-local jump has a registered `LambdaRecoveryFrame`;
- recovery frames nest correctly across eval, dynamic import, callbacks, and
  cross-language execution;
- every landing restores root/number and audited auxiliary watermarks before
  any possible GC;
- no target can remain armed after its native/JIT activation has returned;
- C14 faults reach exactly one nearest eligible `pn` `^err` boundary, or the
  global execution handler after every intervening transaction barrier restores;
- OOM, RootFrame/number-home exhaustion, and equality-depth faults allocate
  neither a rich error nor a second diagnostic before landing;
- an OOM while constructing an ordinary error reports the static OOM fault and
  retains the original code only as non-allocating prior-error metadata;
- stack overflow is not intercepted by a generic event-loop/batch handler in
  production mode;
- forced GC immediately after each landing point passes in precise-only mode;
- timeout/crash/MIR-error batch protocol and retry behavior remain unchanged;
- arbitrary memory faults are not silently continued in production;
- ASan/UBSan, deep-recursion, OOM injection, RootFrame exhaustion,
  equality-depth, nested-callback, async, hosted-guest, Test262, Node,
  Lambda baseline, and Radiant event-loop tests pass;
- no obsolete global/static jump buffer remains without a documented
  library-local exception.

## 14. Source map

| Concern | Current source |
|---|---|
| Shared stack-overflow buffer/armed flag | `lambda/runtime/lambda-stack.cpp`, `lambda/runtime/lambda-stack.h` |
| Unix/Windows stack-overflow jump | `lambda/runtime/lambda-stack.cpp` |
| Normal Lambda landing/restore | `lambda/runtime/runner.cpp` |
| Cached MIR landing/restore | `lambda/runtime/transpile-mir.cpp` |
| Direct LambdaJS landing/restore | `lambda/js/js_mir_entrypoints_require.cpp` |
| Hosted Jube guest landing/restore | `lambda/jube/jube_registry.cpp` |
| Shared side-stack checkpoint | `lambda/runtime/side_stack.h`, `lambda/runtime/side_stack.c` |
| Batch timeout/crash/MIR buffers and handlers | `lambda/main.cpp` |
| Production event-loop crash policy | `lambda/js/js_event_loop.cpp` (no SIGSEGV recovery target) |
| Eval/module/task watermarks to audit | `lambda/js/js_runtime_state.cpp`, `lambda/runtime/concurrency.cpp` |
| MinGW setjmp intrinsic shim | `lambda/main.cpp` |
| Library-local PNG recovery | `lib/image.c:100`, `:196`, `:463`, `:492`, `:720` |

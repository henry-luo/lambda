# Lambda execution recovery: current non-local-jump inventory and redesign ledger

**Status:** CURRENT-STATE INVENTORY — redesign required in a future session;
no implementation is authorized by this document

**Date:** 2026-07-17

**Companion documents:**

| Document | Relationship |
|---|---|
| `vibe/Lambda_Design_Stack_Rooting.md` | RH8 requires exact watermark restoration across every non-local unwind |
| `vibe/Lambda_Stack_Safety.md` | Defines the signal-based stack-overflow mechanism that uses the shared recovery target |
| `vibe/Lambda_Design_Stack_Frame.md` | Defines root/number frame lifetime and normal epilogue invariants that a jump bypasses |

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
| Stack overflow, Unix | `lambda-stack.cpp:191` `siglongjmp(_lambda_recovery_point, 1)` | `runner.cpp:1546`; `transpile-mir.cpp:14722`; `js_mir_entrypoints_require.cpp:989` | first two restore; direct JS entrypoint does not |
| Stack overflow, Windows | `lambda-stack.cpp:259` `longjmp(_lambda_recovery_point, 1)` | Windows branches of the same three landing families | same split |
| Simple file-batch timeout | `main.cpp:1371` | `main.cpp:3898` | no local snapshot/restore; later runtime reset |
| Test262 hot-batch timeout | `main.cpp:1371` | `main.cpp:4235` | common per-test snapshot/restore |
| Test262 hot-batch crash | `main.cpp:1360` | `main.cpp:4209` | common per-test snapshot/restore; interrupted resources intentionally leak until cleanup/reset |
| Test262 MIR error | `main.cpp:1347` | `main.cpp:4234`, `main.cpp:4263` | common per-test snapshot/restore |
| JS event-loop SIGSEGV guard | `js_event_loop.cpp:1586` | `js_event_loop.cpp:1936` | no root/number snapshot or restore |
| Conservative-GC register flush | no jump; `lambda-mem.cpp:279` calls `setjmp(regs)` | none | not recovery; exists only to expose registers to native-stack scanning |
| MinGW intrinsic shim | `main.cpp:278` calls `setjmp(env)` | caller owned by the Windows/MIR ABI | compatibility shim, not a Lambda recovery policy |
| PNG/image errors | `lib/image.c`, including explicit `longjmp` at line 463 | local decode/encode `setjmp` sites | library-local; does not target Lambda recovery state |

## 3. Stack-overflow recovery

### 3.1 Shared thread-local target

`lambda/lambda-stack.cpp` defines one thread-local recovery buffer and one
armed flag:

```text
Unix       __thread sigjmp_buf _lambda_recovery_point
Windows    __thread jmp_buf    _lambda_recovery_point
all        __thread volatile sig_atomic_t _lambda_recovery_armed
```

The signal/SEH handler jumps only when `_lambda_recovery_armed` is true.
Otherwise it restores/defaults the signal handling and terminates rather than
jumping into an uninitialized buffer.

Current jump origins:

- Unix/macOS/Linux stack-overflow signal handler:
  `lambda/lambda-stack.cpp:191` —
  `siglongjmp(_lambda_recovery_point, 1)`;
- Windows unhandled-exception filter:
  `lambda/lambda-stack.cpp:259` —
  `longjmp(_lambda_recovery_point, 1)`.

### 3.2 Normal Lambda runner landing point

`lambda/runner.cpp:1543–1565`:

1. snapshots `side_root_top` and `side_number_top` through
   `LambdaSideStackSnapshot`;
2. establishes `_lambda_recovery_point`;
3. arms recovery only around `main_func(context)`;
4. after a jump, disarms recovery and restores the side-stack snapshot;
5. records `ItemError` and reports stack overflow.

This is the most complete current example of the RH8 behavior.

### 3.3 Cached Lambda MIR landing point

`lambda/transpile-mir.cpp:14719–14738` uses the same pattern for cached MIR
execution: snapshot, establish target, arm around generated execution, restore
after a jump, and convert the result to `ItemError`.

### 3.4 Direct LambdaJS MIR landing point

`lambda/js/js_mir_entrypoints_require.cpp:989–1005` establishes and arms the
same `_lambda_recovery_point` around `js_main`, but its jump branch does **not**
snapshot or restore root/number watermarks. It converts the result to
`ITEM_ERROR` and throws a JS `RangeError`, then continues through later JS/MIR
cleanup.

This is a current recovery gap. A generated epilogue skipped by stack overflow
can leave `side_root_top` and `side_number_top` inside an abandoned activation
until an outer batch reset or context teardown happens. The entrypoint itself
does not satisfy RH8.

### 3.5 Non-nestable shared target

All three landing families overwrite the same thread-local jump buffer and
Boolean armed flag. There is no save/restore of a previous recovery target.
Nested generated execution — for example eval, dynamic module execution, or a
re-entrant guest call — can establish an inner target, then disarm it on return
without restoring the outer target/armed state.

Consequences to verify in redesign:

- an outer execution may no longer catch a later stack overflow;
- a timeout or other jump can bypass `_lambda_recovery_armed = 0`, leaving the
  flag true with a stale target;
- a single Boolean cannot represent nested armed recovery scopes;
- the target is thread-local, but some other recovery buffers are process-
  global, so the recovery models do not share one threading contract.

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

## 7. JS event-loop SIGSEGV recovery

`lambda/js/js_event_loop.cpp:1575–1602` defines a static
`event_loop_jmpbuf` and installs `event_loop_sigsegv_handler()` during event
loop drain. When guarded, the handler executes:

```text
longjmp(event_loop_jmpbuf, 1)
```

The landing point is `lambda/js/js_event_loop.cpp:1936`; it records `-1`,
restores the previous SIGSEGV handler, and returns from the drain.

Current gaps:

- no root/number watermark snapshot or restore;
- generated callback epilogues and native helper destructors can be skipped;
- uses ordinary `setjmp`/`longjmp` across a signal handler instead of the
  signal-mask-preserving `sigsetjmp`/`siglongjmp` pair;
- installs a temporary SIGSEGV handler without `SA_ONSTACK`, so a true stack
  overflow during drain can be intercepted by this crash guard rather than the
  dedicated alternate-stack overflow handler;
- performs logging and handler manipulation in the signal handler, which
  requires a separate async-signal-safety audit;
- catches arbitrary memory faults attributed in comments to a pre-existing
  timer-callback bug, potentially allowing corrupted process state to
  continue.

This mechanism should not be carried unchanged into the shared recovery
design. Prefer fixing the underlying crash and isolating arbitrary-fault
containment at the test/process boundary. If local containment remains, it
must use the same explicit recovery checkpoint and state-restoration contract.

## 8. `setjmp` uses that are not Lambda execution recovery

### 8.1 Conservative GC register flush

`lambda/lambda-mem.cpp:276–279` executes:

```text
jmp_buf regs
setjmp(regs)
```

There is no corresponding `longjmp(regs, ...)`. The call attempts to spill
callee-saved registers into native-stack memory before conservative scanning.
It is not an error-recovery boundary and is removed when conservative native-
stack scanning is retired.

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
| Normal Lambda stack overflow | restored | restored | disarmed; target not nested | converts to `ItemError` |
| Cached MIR stack overflow | restored | restored | disarmed; target not nested | converts to `ItemError` |
| Direct LambdaJS stack overflow | **not locally restored** | **not locally restored** | disarmed; target not nested | throws JS `RangeError`, continues cleanup |
| Simple batch timeout | **no local restore** | **no local restore** | may bypass `_lambda_recovery_armed` cleanup | runtime reset afterward |
| Test262 hot-batch timeout | restored by common test path | restored | batch flags reset; Lambda recovery flag not centrally restored | interrupted context may be recreated |
| Test262 batch crash | restored by common test path | restored | batch crash flag re-enabled | intentional leak containment + possible context reset |
| Test262 MIR error | restored by common test path | restored | MIR flag reset | skips intervening MIR/native cleanup |
| JS event-loop SIGSEGV | **not restored** | **not restored** | separate static buffer; prior SIGSEGV handler restored | returns `-1` after skipped frames |

The matrix covers only root/number watermarks and obvious recovery flags.
Future inventory must include any JS argument-stack mark, active module/eval
context, active MIR context, scheduler/callback state, current `Context*`, and
other dynamic state that can change between checkpoint and jump.

## 10. Structural design problems

### ER1 — Multiple unrelated recovery mechanisms

Stack overflow, timeout, batch crash, MIR error, and event-loop crash each
define their own buffer, active flag, handler, result convention, and cleanup.
There is no common recovery record or required state snapshot.

### ER2 — Recovery targets are not consistently nestable

`_lambda_recovery_point` has one TLS slot and one Boolean. Batch and event-loop
buffers are static globals. Nested execution and multiple threads do not have
one explicit stack discipline.

### ER3 — Watermark restoration is incomplete

Only the normal Lambda runner, cached MIR path, and Test262 common path restore
`LambdaSideStackSnapshot`. Direct JS, simple timeout, and event-loop recovery
do not.

### ER4 — Handler ownership conflicts

The stack-overflow system, Test262 batch, and event-loop drain all install
SIGSEGV handlers. The last installed handler wins. A true stack overflow can
therefore be classified as a generic batch/event-loop crash, while an
arbitrary invalid access can enter a local recovery path designed for another
failure class.

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
crash becomes `128 + signal`; MIR errors become result `1`; event-loop crash
becomes `-1`. These are reasonable at their boundaries but need one typed
internal reason so cleanup does not infer behavior from ad-hoc integers.

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

## 12. Suggested future implementation sequence

1. **ER-S0: freeze this inventory.** Add focused tests for every existing
   origin/landing pair and record current result codes and cleanup behavior.
2. **ER-S1: implement recovery-frame push/pop without changing handlers.**
   Support nesting and common state snapshots; keep old buffers behind adapters.
3. **ER-S2: migrate stack-overflow landing points.** Replace the single TLS
   buffer/Boolean with the recovery-frame top; close the direct-JS restoration
   gap.
4. **ER-S3: migrate batch timeout and MIR error.** Preserve batch protocol
   while sharing typed reasons and common restoration.
5. **ER-S4: isolate arbitrary crash containment.** Keep Test262 worker crash
   recovery explicitly test-only; remove or redesign the event-loop SIGSEGV
   guard after fixing its underlying crash.
6. **ER-S5: audit auxiliary watermarks and nested execution.** Cover eval,
   dynamic import, callbacks, scheduler drain, module initialization, and
   cross-language re-entry.
7. **ER-S6: enable precise-rooting recovery gates.** Force GC immediately
   after every landing point and verify no abandoned frame remains visible.
8. **ER-S7: remove obsolete buffers/flags and update stack-safety/rooting
   documents.**

## 13. Acceptance gates for the redesign

- every non-local jump has a registered `LambdaRecoveryFrame`;
- recovery frames nest correctly across eval, dynamic import, callbacks, and
  cross-language execution;
- every landing restores root/number and audited auxiliary watermarks before
  any possible GC;
- `_lambda_recovery_armed` cannot remain true with a stale/dead target;
- stack overflow is not intercepted by a generic event-loop/batch handler in
  production mode;
- forced GC immediately after each landing point passes in precise-only mode;
- timeout/crash/MIR-error batch protocol and retry behavior remain unchanged;
- arbitrary memory faults are not silently continued in production;
- ASan/UBSan, deep-recursion, timeout, nested-callback, async, Test262, Node,
  Lambda baseline, and Radiant event-loop tests pass;
- no obsolete global/static jump buffer remains without a documented
  library-local exception.

## 14. Source map

| Concern | Current source |
|---|---|
| Shared stack-overflow buffer/armed flag | `lambda/lambda-stack.cpp`, `lambda/lambda-stack.h` |
| Unix/Windows stack-overflow jump | `lambda/lambda-stack.cpp:191`, `:259` |
| Normal Lambda landing/restore | `lambda/runner.cpp:1543–1565` |
| Cached MIR landing/restore | `lambda/transpile-mir.cpp:14719–14738` |
| Direct LambdaJS landing without restore | `lambda/js/js_mir_entrypoints_require.cpp:989–1005` |
| Batch timeout/crash/MIR buffers and handlers | `lambda/main.cpp:1331–1373` |
| Simple batch timeout landing | `lambda/main.cpp:3898` |
| Test262 per-test snapshot and landing points | `lambda/main.cpp:4199–4309` |
| Test262 crash cleanup/reset policy | `lambda/main.cpp:4324–4390` |
| Event-loop SIGSEGV jump/landing | `lambda/js/js_event_loop.cpp:1575–1602`, `:1927–1970` |
| Conservative GC register flush | `lambda/lambda-mem.cpp:276–279` |
| MinGW setjmp intrinsic shim | `lambda/main.cpp:274–279` |
| Library-local PNG recovery | `lib/image.c:100`, `:196`, `:463`, `:492`, `:720` |

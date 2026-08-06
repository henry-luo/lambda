# Lambda — Execution Recovery Design

**Status:** DESIGN SETTLED, IMPLEMENTED ON POSIX. ER-D1…ER-D13 are the decision set; ER-S0…ER-S7
implemented them (Appendix A). The Windows SEH path shares the frame ABI but its integration run
is still outstanding. **Five open hazards, one of them blocking** — see §5.

**Date:** 2026-07-17, rev 2 2026-08-06 (restructured from inventory-ledger to design record;
inventory re-verified against the live tree; hazards H1–H9 added).

**Companion documents:**

| Document | Relationship |
|---|---|
| `vibe/Lambda_Design_Stack_Rooting.md` | RH8 requires exact watermark restoration across every non-local unwind |
| `vibe/Lambda_Stack_Safety.md` | Defines the signal-based stack-overflow mechanism routed through the recovery frame |
| `vibe/Lambda_Design_Stack_Frame.md` | Defines root/number frame lifetime and the normal epilogue invariants a jump bypasses |
| `vibe/Lambda_Design_Type_Enforcement.md` | TE-16/TE-18 own the language surface; C14 defines the unchecked system-fault channel |
| `vibe/Lambda_Impl_Error_Handling.md` | Consumes this machinery — `e ^ { … }` inherits the local-fault frame |

---

## 1. Scope

**In scope:** every non-local control transfer in the Lambda core runtime, the LambdaJS runtime,
and the Jube hosted-guest bridge — stack overflow, resource faults, MIR errors, batch timeout,
and batch crash containment.

**Out of scope, deliberately:**

- **Vendored libraries.** MIR, Tree-sitter, libpng/libjpeg, ThorVG, re2, curl. libpng's
  `setjmp(png_jmpbuf(...))` sites are library-local error paths that target buffers owned by the
  same decode/encode call and never touch Lambda recovery state. Per repo rule 16 they are not
  patched, and they are not merged into this policy.
- **Radiant.** `script_runner.cpp` and `cmd_layout.cpp` each own a process-global guard buffer.
  They are host-application crash containment, not language recovery. One cross-cutting hazard
  applies to them (H2) and is recorded here, but their policy belongs to the Radiant docs.
- **Test harness code** under `test/`.

## 2. Why non-local jumps need an explicit contract

```text
normal return / error branch
    generated epilogue restores root and number watermarks
    C++ destructors restore helper RootFrames

longjmp / siglongjmp
    generated epilogues are skipped
    C++ destructors are skipped
    native cleanup between origin and landing is skipped
```

Every non-local jump that can cross generated or native root frames must therefore restore all
affected runtime watermarks **explicitly at its landing point**. Passing tests while relying on
later context destruction is not a recovery contract — it is a leak that happens to be invisible.
This single observation is what the rest of the design serves.

---

## 3. Design decisions

### ER-D1 — One mechanism: a TLS LIFO of explicit recovery frames

There is exactly one non-local-jump mechanism in the runtime: `LambdaRecoveryFrame`, a per-thread
LIFO. It replaced four independent buffers plus a boolean armed flag, which were non-nestable —
the runner, cached MIR, direct JS, and hosted Jube entries all overwrote one target.

```text
LambdaRecoveryFrame
    previous            // TLS LIFO link; never process-global
    discarded_inner     // retired after a transaction-priority landing
    Context* context    // exact EvalContext owner
    checkpoint          // exact restoration state (ER-D5)
    fault               // embedded FaultRecord; no allocation on fault (ER-D6)
    jump_buffer         // sigjmp_buf on POSIX, jmp_buf on Windows
    capabilities        // capability mask (ER-D2)
    state               // prepared | armed | landed | disarmed
    signal_armed        // volatile sig_atomic_t, read by the signal origin
    signal_fault_reason // volatile sig_atomic_t, written by the signal origin
```

TLS, never process-global, so no jump crosses a thread boundary. A worker publishes only an
already-materialized fault result after its own landing.

### ER-D2 — Frames are capability-typed, and selection is by capability

```text
LAMBDA_RECOVERY_CAP_LOCAL_FAULT          a local `^err` / `^ { }` landing
LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY   an execution entry
LAMBDA_RECOVERY_CAP_TRANSACTION_BARRIER  module init, hosted-guest entry
LAMBDA_RECOVERY_CAP_TEST_CONTAINMENT     test-only
```

A fault selects the **nearest frame that accepts it**, with one override: **an enclosing
transaction barrier takes priority over every inner local handler.** A local handler must never
resume through a half-initialized module or an abandoned guest activation. On a
transaction-priority landing the barrier becomes the TLS top, restores its checkpoint, and
retires the abandoned inner chain via `discarded_inner`.

The selected frame is **unlinked before** any user handler, allocation, or reporting runs, so a
second fault inside a handler targets the *outer* frame and never a stale or re-entered buffer.

### ER-D3 — The `setjmp` locality rule

`setjmp`'s dynamic extent is the function containing the call. **No helper may call `setjmp` and
return before its buffer is jumped to.** Push/pop, snapshot capture, and fault publication may be
C/C++ helpers; the checkpoint itself may not. Every execution entry establishes its own
checkpoint in its own native activation, and the MIR emitter imports the platform primitive
**directly** — it must not simulate this with a returned helper or an RAII wrapper.

`LAMBDA_RECOVERY_FRAME_SETJMP` is deliberately a macro containing no helper call, and
`LAMBDA_RECOVERY_FRAME_JUMP_BUFFER_OFFSET` exists so MIR can hand the buffer address to the
primitive. See H3 for the cost of this decision.

### ER-D4 — Typed fault reasons, a closed set

```text
STACK_OVERFLOW
SIDE_STACK_EXHAUSTION        // RootFrame or number-home reservation
OUT_OF_MEMORY
EQUALITY_DEPTH_EXHAUSTION
RUNTIME_BOUNDARY_DEFECT      // compiler-inserted fail-closed guard only
```

The landing policy maps a typed reason to `ItemError`, a JS exception, a CLI exit status, a batch
protocol code, or process termination. Cleanup never infers behaviour from an ad-hoc integer —
the pre-design convention (`124` for timeout, `128 + signal` for crash, `1` for MIR error) is a
*boundary* convention only, never an internal one.

### ER-D5 — Exact restoration before fault observation

The checkpoint is captured before entering protected work and restored **before** the landing
branch stores an `ItemError`, runs a handler, logs, allocates, drains tasks, or permits GC. A
field absent from the checkpoint may not be repaired later by context destruction.

Skipped C++ `RootFrame` destructors are harmless only because their slots sit above the restored
exact watermark. There is no conservative native-stack scan to mask a missed field — see
CLAUDE.md rule 15.

Mandatory fields, in the order they were specified:

1. `Context*` identity and the TLS active-evaluator owner;
2. `side_root_top` and `side_number_top` with no stale RootFrame slots;
3. MIR return-lane and scalar-home extent, plus every JIT activation watermark below the frame;
4. LambdaJS argument-frame, CommonJS/module, and eval-source stack depths;
5. scheduler current-task/async-frame cursor and callback-dispatch state;
6. hosted-guest activation ownership and module-initialization state.

Items 1–3 are in the common `LambdaRecoveryCheckpoint`. **Items 4–6 are not** — see H5.

### ER-D6 — Fault delivery cannot allocate

Every frame embeds a pre-initialized `FaultRecord`, and each active `Context` has a pre-reserved
global fallback. A record holds a reason enum, an optional fixed prior-error code, static source
identity, and a non-owning prebuilt `LambdaError`/`ItemError` view, explicitly marked static so
`err_free` never releases it. No stack trace, string formatting, map construction, or heap
allocation happens on the origin or landing path; `err.message` is backed by a pre-reserved
string so a handler can read it without allocating.

**The OOM rule.** If constructing an ordinary rich error fails, the primary error is discarded and
the runtime raises `OUT_OF_MEMORY` with `prior_error_code` populated. It must never recursively
attempt a second rich error. `set_runtime_error_no_trace()` is not valid on this path because
`err_create()` allocates.

### ER-D7 — Signal-handler discipline

A signal or SEH handler performs **only** minimal classification, stores enum-sized state in the
preselected frame, and jumps. It does not log, allocate, lock, alter handlers, construct an
error, or call anything not async-signal-safe. Logging, error creation, heap reset, MIR cleanup,
and handler restoration all happen at the landing, where ordinary runtime operations are safe.

An unarmed or non-stack signal takes the fail-stop path (`signal(sig, SIG_DFL); raise(sig);`).

### ER-D8 — No reliance on destructors across jumps

Any resource that must survive a jump is either represented in the checkpoint or owned outside
the jumped-over region. RAII remains correct for structured exits but is never cited as cleanup
for a `longjmp` path.

### ER-D9 — Faults are unchecked and never enter function types

A system fault is C14's third channel. It contributes no `error` constituent to any function
type, is not a `T | error` value, and is not a normal call-result ABI. It is observable only at
the selected local boundary, through the existing `ItemError` marker. A caught fault **cannot
resume the abandoned expression**.

An `fn` never installs a system-fault landing point — faults pass transparently through `fn`
frames. Only `pn` local boundaries and execution boundaries own them.

### ER-D10 — Containment policy: production fail-stop, test containment separate

Recoverable faults (stack overflow, resource exhaustion, controlled timeout, MIR error) use
recovery frames. **Arbitrary memory faults terminate the process in production** and are never
recast as language `ItemError` values. The production JS event loop owns no SIGSEGV target at
all.

Test262-style containment of arbitrary `SIGSEGV`/`SIGABRT` is useful for a *test worker* and
unsafe in a long-lived process. It stays at the batch/worker boundary behind an explicit
test-only capability, and never becomes a language-catchable fault. See H1 — this separation is
currently violated in practice.

### ER-D11 — Re-entry: modules, async, workers, guests

- **Nested eval / import / module execution.** Every execution entry pushes an execution-boundary
  frame and restores the previous target on normal return. Module and eval initialization is a
  **transaction barrier**: it restores, marks the partial module failed or discardable, then
  forwards the same fault outward.
- **Async tasks.** Recovery frames are native-stack local and **never survive a yield.** Each
  task poll establishes its own boundary; a fault either lands in a non-suspending local
  boundary or completes that task with the static fault result. It can never jump into a
  resumed activation from an earlier poll. This is the constraint that produced ER-D13.
- **Callbacks and event-loop drain.** A callback runs under the frame active for that drain.
- **Workers.** Frame stack and fallback record are TLS; no jump crosses a worker boundary.
- **Hosted Jube guests.** Guest entry is an execution boundary **and** a transaction barrier. A
  landing restores guest-side watermarks, publishes the static fault, then **poisons** the
  guest's frame-runtime and scalar-home slots so later guest calls reject the abandoned
  activation.

### ER-D12 — Platform rules

POSIX uses `sigaction(..., SA_SIGINFO | SA_ONSTACK)` with a dedicated `sigaltstack` (the handler
must run off the exhausted stack), plus `sigsetjmp`/`siglongjmp`. Windows uses the matching TLS
frame selection through `SetUnhandledExceptionFilter` and `longjmp` after `_resetstkoflw`.
**The Windows behaviour is not inferred from POSIX results** — it requires its own integration
run (H4).

### ER-D13 — A recovery frame may not span an async suspension (2026-08-06)

The jump buffer records a machine context inside the *currently executing* activation. An `await`
unwinds the native stack to the scheduler and resumes on a different frame, so a buffer captured
before the suspension points at dead stack.

**Decision: a local fault handler over a possibly-suspending expression is a compile error.**
`AstNamedNode.local_fault_safe` and `classify_error_destructure_fault_boundary_node` already
compute the predicate; today they *silently* degrade to value-error-only handling, which the
language surface (TE-16) now makes a diagnostic instead. Rejected alternative: split the
capability silently by operand shape — same syntax, different fault behaviour, decided by
something invisible in the source.

Follow-on, not now: **segment the protected region per poll** — split at each `await`, arm one
frame per segment. The handler label is stable within the function, so every segment lands in the
same place and no buffer crosses a poll. This avoids crossing polls rather than supporting it,
and is backward-compatible with code written under the rejection. Full rationale in
`Lambda_Design_Type_Enforcement.md` TE-16.

---

## 4. Inventory (re-verified 2026-08-06)

### 4.1 Recovery-frame establishment sites — eight

| # | Site | Capability | Role |
|---|---|---|---|
| 1 | [`runner.cpp:1483`](../lambda/runtime/runner.cpp#L1483) | EXECUTION_BOUNDARY | normal Lambda runner around `main_func` |
| 2 | [`transpile-mir.cpp:21868`](../lambda/runtime/transpile-mir.cpp#L21868) | EXECUTION_BOUNDARY | cached-MIR execution |
| 3 | [`transpile-mir.cpp:21901`](../lambda/runtime/transpile-mir.cpp#L21901) | TRANSACTION_BARRIER | module initialization |
| 4 | [`transpile-mir.cpp:8781`](../lambda/runtime/transpile-mir.cpp#L8781) | LOCAL_FAULT | **MIR-emitted** local `^err` checkpoint |
| 5 | [`js_mir_entrypoints_require.cpp:1057`](../lambda/js/js_mir_entrypoints_require.cpp#L1057) | EXECUTION_BOUNDARY | direct LambdaJS MIR around `js_main` |
| 6 | [`concurrency.cpp:755`](../lambda/runtime/concurrency.cpp#L755) | EXECUTION_BOUNDARY | async task poll |
| 7 | [`jube_registry.cpp:2854`](../lambda/jube/jube_registry.cpp#L2854) | EXECUTION_BOUNDARY \| TRANSACTION_BARRIER | hosted guest entry (form 1) |
| 8 | [`jube_registry.cpp:2901`](../lambda/jube/jube_registry.cpp#L2901) | EXECUTION_BOUNDARY \| TRANSACTION_BARRIER | hosted guest entry (form 2) |

Site 4 is the only JIT-emitted one and the only `LOCAL_FAULT` frame. It is what `e ^ { … }`
inherits, and the reason ER-D13 exists.

### 4.2 Fault origins

| Origin | Mechanism | Notes |
|---|---|---|
| Stack overflow, POSIX | [`lambda-stack.cpp:206`](../lambda/runtime/lambda-stack.cpp#L206) `siglongjmp` | `SA_SIGINFO \| SA_ONSTACK`, fault-address disambiguation, own `sigaltstack` |
| Stack overflow, Windows | [`lambda-stack.cpp:297`](../lambda/runtime/lambda-stack.cpp#L297) `longjmp` after `_resetstkoflw` | inside `SetUnhandledExceptionFilter`; same frame selection |
| Resource faults | `lambda_recovery_frame_raise_fault` / `_raise_local_fault` | ordinary calls, no signal involved |
| JIT-side origination | `sigsetjmp`/`setjmp` exported via [`sys_func_registry.c:1302`](../lambda/runtime/sys_func_registry.c#L1302) | ER-D3; see H3 |

### 4.3 Batch containment — test-only, separate by design (ER-D10)

Three process-global buffers in `main.cpp`, each with a `volatile sig_atomic_t` active flag:

| Buffer | Origin | Landing | Restoration |
|---|---|---|---|
| `batch_timeout_jmp` ([:1450](../lambda/main.cpp#L1450)) | `SIGALRM` → [:1490](../lambda/main.cpp#L1490) | [:3850](../lambda/main.cpp#L3850) simple file batch; [:4236](../lambda/main.cpp#L4236) test262 hot batch | simple batch: **none** (runtime reset instead); hot batch: common per-test restore |
| `mir_error_jmp` ([:1454](../lambda/main.cpp#L1454)) | MIR error callback → [:1466](../lambda/main.cpp#L1466) | [:4235](../lambda/main.cpp#L4235), [:4267](../lambda/main.cpp#L4267) | common per-test restore; skips intervening MIR/native cleanup |
| `batch_crash_jmp` ([:1473](../lambda/main.cpp#L1473)) | SIGSEGV/SIGBUS/SIGABRT/SIGTRAP → [:1479](../lambda/main.cpp#L1479) | [:4207](../lambda/main.cpp#L4207) | common per-test restore; other interrupted resources intentionally leak |

The hot batch captures `batch_side_stack_snapshot` before establishing any per-test target, and
all four paths (timeout, crash, MIR error, normal) converge on one
`lambda_side_stack_restore(...)`.

### 4.4 Not execution recovery

`__intrinsic_setjmpex` ([`main.cpp:411`](../lambda/main.cpp#L411)) — a MinGW shim delegating to
ordinary `setjmp`. The caller owns the ABI-level jump behaviour; this is a compatibility
artifact, not a policy. Its existence is evidence for H3.

**Retired:** the conservative-GC `setjmp(regs)` register-spill trick (belonged solely to
native-stack scanning, retired with it — CLAUDE.md rule 15), the pre-ER-S2 single
`_lambda_recovery_point` + boolean target, and the event-loop-local `event_loop_jmpbuf`
(`js_event_loop.cpp` now contains zero jump buffers, verified).

---

## 5. Hazards and open issues

### H1 — Batch mode silently replaces the stack-overflow handler *(blocking)*

`main.cpp` calls `lambda_stack_init()` at [:3931](../lambda/main.cpp#L3931), which installs the
`SA_SIGINFO`-based stack-overflow handler — then at
[:3977-3980](../lambda/main.cpp#L3977) the batch loop overwrites `SIGSEGV`, `SIGBUS`, `SIGABRT`
and `SIGTRAP` with `batch_crash_handler`. Because `install_signal_handler` is guarded by
`_signal_handler_installed`, it never reinstalls.

`install_signal_handler` also passes **NULL** as `sigaction`'s third argument, so it neither
saves nor chains a previous handler. Batch mode *does* save and restore (`old_segv_sa` etc.), so
the net effect is one-directional: **for the whole batch loop, a stack overflow in JIT code lands
in `batch_crash_jmp` and is reported as `128 + signal`, instead of reaching the armed
`LambdaRecoveryFrame` and becoming the language-level `ItemError` stack-overflow result.**

Two further consequences: `batch_crash_handler` uses `sa_handler` rather than `SA_SIGINFO`, so it
cannot disambiguate a stack-overflow fault address from an arbitrary segfault, losing the
classification ER-D7 relies on; and both regimes install their own `sigaltstack`, the second
replacing the first and orphaning the earlier allocation.

This directly violates ER-D10's separation, and it means **fault-capture behaviour differs
between `make test` batch mode and standalone execution.** Fix before writing fault-capture tests
for `e ^ { … }`, or those tests will not measure what they appear to.

### H2 — Process-global jump buffers outside the TLS design

ER-D1 mandates TLS, and `LambdaRecoveryFrame` complies. The three `main.cpp` batch buffers do
not, and neither do Radiant's `js_exec_jmpbuf` and `layout_crash_jmpbuf` (out of scope for policy,
in scope for this hazard). All are plain `static`.

Single-threaded today. Under the concurrency work (`Lambda_Design_Radiant_Concurrency` RC1–RC8
isolates, `Lambda_Js_Thread` JT1–JT7) two threads in guarded execution would clobber each other's
buffer and longjmp into a dead frame. Cheapest fix: `__thread`.

### H3 — `setjmp` as an imported symbol is formally UB, and MIR must honour returns-twice

ER-D3 requires the JIT to call the platform primitive directly, so
[`sys_func_registry.c:1302`](../lambda/runtime/sys_func_registry.c#L1302) exports `sigsetjmp`
(POSIX) / `setjmp` (Windows) as a JIT-callable symbol. C11 7.13.1.1 restricts where `setjmp` may
appear; invoking it through an import is outside that set.

It works because `sigsetjmp` is a genuine out-of-line function whose saved context is the
*caller's* — the MIR activation, which is exactly what ER-D3 wants. Two fragilities:

1. It breaks wherever `setjmp` is a compiler builtin or macro. The `__intrinsic_setjmpex` shim
   (§4.4) suggests this already bit on MSVC.
2. **MIR must treat the call as returning twice.** Any local live across the checkpoint must be
   memory-backed, or the second return reads a stale register. This is silent when wrong.
   Needs a dedicated MIR emission test — the existing tests would pass with a miscompile.

### H4 — Windows SEH path has never been executed

ER-S7 completed the POSIX and precise-root gates. The Windows branch shares the frame ABI and
selection logic but its integration run remains outstanding, and ER-D12 explicitly forbids
inferring it from POSIX results. Until it runs, Windows fault recovery is untested, not
"probably fine".

### H5 — The common checkpoint covers items 1–3 of ER-D5, not 4–6

`LambdaRecoveryCheckpoint` ([`side_stack.h:25`](../lambda/runtime/side_stack.h#L25)) holds
`Context*`, the side-stack snapshot, `mir_return_lane`, and `mir_bitcast_scratch` — ER-D5 items
1, 2 and 3.

Items 4 (LambdaJS argument-frame / module / eval depths), 5 (scheduler cursor and
callback-dispatch state) and 6 (guest activation ownership) are handled **ad hoc at individual
landing sites**: Jube poisons its own slots, module frames reset partial slabs, task polls own a
static result. That is exactly the per-entrypoint arrangement ER-D5 said to avoid, and it means a
*new* execution entry can be added without inheriting items 4–6. Consolidating them into the
common checkpoint is the outstanding half of ER-D5.

### H6 — Simple file-batch timeout has no restoration at all

The `main.cpp:3850` landing has no local side-stack snapshot; it relies on a subsequent
`runtime_reset_heap()`. That is teardown-as-containment rather than structured restoration, and
it can bypass the stack-overflow runner's disarm assignment. Acceptable only because the process
is about to reset — but it is the one remaining path that contradicts §2, and it should either
gain a snapshot or be documented as an explicit terminal path.

### H7 — Crash containment leaks by design

Comments estimate roughly 55 MB of MIR/AST/temporary state per recovered crash, since interrupted
resources never run their cleanup. The batch enforces crash-count and RSS limits and may
destroy and recreate its heap and context. This is intentional under ER-D10 (test-worker
containment), and is recorded so it is not mistaken for a leak regression.

### H8 — `sigsetjmp(env, 1)` costs a syscall per armed frame

The `savemask = 1` argument makes every arm perform a `sigprocmask`. Fine at today's `^err`
frequency. If `e ^ { … }` becomes the primary error-handling surface and arms frames routinely,
this lands on a warm path. Either audit whether `savemask = 0` is safe inside protected regions,
or arm lazily. Measure before the handler work lands, not after.

### H9 — `crash_sa` is not zero-initialized

[`main.cpp:3973`](../lambda/main.cpp#L3973) declares `struct sigaction crash_sa;` and sets
`sa_handler`, `sa_mask` and `sa_flags` but never `memset`s the struct, leaving any additional
platform members indeterminate. Minor and probably harmless on glibc/macOS; a one-line fix.

---

## Appendix A — Implementation history (ER-S0…ER-S7)

Complete on POSIX as of 2026-07-31.

| Stage | Content | Status |
|---|---|---|
| ER-S0 | Lock the fault contract: pre-reserved fault record, testable allocation-failure rule | complete |
| ER-S1 | TLS frame ABI plus exact common snapshot; nested-frame, forced-GC, exhaustion tests | complete |
| ER-S2 | Migrate all execution entries to the frame top; normal nested return proves the outer target stays armed | complete |
| ER-S3 | MIR emits a real checkpoint only for a non-suspending RHS; native root-fault test proves local-then-outer selection | complete |
| ER-S4 | Stack/RootFrame/OOM origins transfer static records; rich-error allocation failure becomes static OOM; equality-depth takes the static path for a live procedural handler | complete |
| ER-S5 | Task-poll and module transaction boundaries; hosted guests poison abandoned runtime/scalar slots | complete |
| ER-S6 | Production event loop no longer replaces SIGSEGV handling or continues after arbitrary corruption | complete |
| ER-S7 | Precise-root and POSIX recovery gates | POSIX complete; **Windows run outstanding (H4)** |

## Appendix B — Structural problems that motivated the design

Recorded with resolution, so the reasoning is not re-litigated.

| # | Problem | Resolved by | Residue |
|---|---|---|---|
| ER1 | Five unrelated mechanisms, each with its own buffer, flag, handler, result convention and cleanup | ER-D1, ER-D4 | batch containment stays separate *by design* (ER-D10) |
| ER2 | Execution targets were not nestable — one buffer, one boolean | ER-D1 (TLS LIFO) | none |
| ER3 | Watermark restoration incomplete | ER-D5 | **H5** (items 4–6), **H6** (simple batch timeout) |
| ER4 | Handler ownership conflicts between stack-overflow and batch systems | ER-D10 | **H1 — not actually resolved in code** |
| ER5 | Jumps skip destructors, epilogues, MIR teardown, libuv cleanup | ER-D8 | inherent; mitigated by checkpoint coverage |
| ER6 | Signal-handler safety inconsistent (some logged, some altered handlers) | ER-D7 | none in the language path |
| ER7 | Recovery result semantics differed by mechanism | ER-D4 | boundary conventions retained deliberately |
| ER8 | Test containment mixed with production recovery | ER-D10 | **H1** |

## Appendix C — Restoration matrix

| Path | Root top | Number top | Target handling | Other |
|---|---|---|---|---|
| Lambda runner stack overflow | restored | restored | frame popped before report; outer resumes | → `ItemError` |
| Cached MIR stack overflow | restored | restored | frame popped before report; outer resumes | → `ItemError` |
| Direct LambdaJS stack overflow | restored | restored | frame popped before report; outer resumes | → JS `RangeError` |
| Hosted Jube stack overflow | restored | restored | guest frame popped; outer resumes | → guest `ItemError`; guest slots poisoned |
| Async task poll | restored | restored | task frame popped | task completes with static fault result |
| Module init (transaction barrier) | restored | restored | inner chain retired via `discarded_inner` | partial module slabs reset, fault forwarded |
| Test262 hot-batch timeout / crash / MIR error | restored by common path | restored | batch flags reset | context may be recreated; leak accepted (H7) |
| Simple file-batch timeout | **none** | **none** | separate batch target | runtime reset afterward (H6) |
| Production JS event loop | n/a | n/a | active frame only | arbitrary SIGSEGV fails stop (ER-D10) |

## Appendix D — Source map

| Concern | Source |
|---|---|
| Frame ABI, capabilities, macros | `lambda/runtime/recovery_frame.h`, `recovery_frame.c` |
| Signal/SEH origins, handler install | `lambda/runtime/lambda-stack.cpp`, `lambda-stack.h` |
| Checkpoint capture/restore | `lambda/runtime/side_stack.h`, `side_stack.c` |
| Lambda runner landing | `lambda/runtime/runner.cpp` |
| Cached MIR + module landings, MIR-emitted local checkpoint | `lambda/runtime/transpile-mir.cpp` |
| Async task-poll landing | `lambda/runtime/concurrency.cpp` |
| Direct LambdaJS landing | `lambda/js/js_mir_entrypoints_require.cpp` |
| Hosted Jube guest landings | `lambda/jube/jube_registry.cpp` |
| JIT-callable `setjmp` export | `lambda/runtime/sys_func_registry.c` |
| Suspension predicate for local frames | `lambda/runtime/build_ast.cpp` (`classify_error_destructure_fault_boundary_node`), `ast-core.hpp` (`local_fault_safe`) |
| Batch timeout/crash/MIR containment, MinGW shim | `lambda/main.cpp` |
| Production event-loop policy (no jump target) | `lambda/js/js_event_loop.cpp` |

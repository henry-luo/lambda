# Runtime Async — One Suspension Engine for Lambda and LambdaJS, Interpreter and MIR

**Date:** 2026-10-04
**Status:** **PROPOSED — not ratified.** Nothing in `doc/Lambda_Formal_Design.md` or `doc/Lambda_Formal_Semantics.md` has been changed. §11 lists every existing ruling this design would revise; each needs a user ruling before any spec edit or implementation.
**Design authority it must line up with:** `doc/Lambda_Formal_Design.md` — D5.1.1v2, D5.1.3, D5.3.2, D5.3.6, D6.1.3, D6.2.2v2, D6.3.1, D6.3.3, D7.4.2, D8.1.1v15; `doc/Lambda_Formal_Semantics.md` — S7.11, S13.1–S13.2.
**Working designs it amends:** `vibe/Lambda_Design_Concurrency.md` (K2-R, K10, K17, §10.5–§10.6), `vibe/Lambda_Design_Ast_Interpreter.md` (AI11, AI24), `vibe/Lambda_Design_Structs_JS.md` (JSCU25, JSCU26, JSCU32).
**Ledger series:** `RA#` (decisions), `RA-O#` (open items).
**Evidence base:** the line references in §2 come from a code survey on 2026-10-04 against the working tree at `8e81166df`. LOC figures in §9 are estimates derived from identifier counts and region sizes, not measured diffs.

---

## 0. Summary

Lambda and LambdaJS each run in two tiers, giving four cases. Today those four cases suspend in three different ways and one of them cannot suspend at all. The three mechanisms are all *stackless*: each one exists to reconstruct, in a heap object, what the native stack already held.

This document proposes replacing all of them with one *stackful* mechanism: an **Activation** that owns its own native stack and its own side-stack segments. An `await`, `yield`, `wait` or `receive` becomes a context switch out of the activation; a resume is a context switch back in. Code inside the activation is ordinary code in either tier.

The four goals follow from that one change:

- **G1 — one engine.** One record, one switch primitive, one scheduler queue and one loop drain serve Lambda tasks, JS async functions, JS generators, async generators and module top-level await.
- **G2 — the Lambda interpreter runs async procedures directly.** Suspension no longer depends on a compiler transform, so T0 needs no MIR satellite.
- **G3 — tier-up by hotness only.** No tier needs to know whether a function can suspend, so every colour predicate in the promotion logic is deleted, in both languages.
- **G4 — net LOC reduction.** Two state-machine lowerings, the JS interpreter's replay machinery and the Lambda async-frame protocol are deleted. The estimate is a net −4,000 to −6,000 lines (§9).

The design reverses K2-R (state machines over fibers). §4.1 explains why the two facts that decided K2-R no longer hold. It has real costs, chiefly parked-activation memory for JS and a per-architecture context switch; §8 states them and §10 puts a measurement gate in front of the part of the work that is hardest to undo.

---

## 1. Goals and non-goals

**Goals**

- **G1.** One async runtime engine for the four cases: Lambda × {interp, MIR}, JS × {interp, MIR}.
- **G2.** The Lambda AST interpreter executes may-await procedures, `start`, and task-context builtins itself, with no MIR JIT involved.
- **G3.** MIR promotion is decided by hit rate alone. Function colour (async, generator, may-await, task-context) is not an input to tier selection in either language.
- **G4.** The work as a whole removes more code than it adds.
- **G5.** Clean async handling code: one place to look for how anything suspends.

**Non-goals**

- No change to observable semantics. JS job ordering, Lambda's macrotask resume order (D6.3.1), structured-concurrency scopes, mailbox semantics (S13.2) and error values stay as they are.
- No change to the reactor. libuv stays the one loop (K3, K10 layer 1).
- No multi-threaded scheduling of tasks. K7 already rules that tasks never migrate between threads.

---

## 2. What exists today

### 2.1 Four cases, three mechanisms

| Case | Mechanism | Parked state | Resumed by |
|---|---|---|---|
| Lambda, MIR | State machine with re-descent. A may-await procedure returns `ITEM_TASK_SUSPENDED` up the chain (`transpile-mir.cpp:8512`). Every poll re-invokes the root procedure (`concurrency.cpp:1352`); each activation re-enters its frame by a cursor (`lambda_async_frame_enter_current`, `concurrency.cpp:1160`) and dispatches on a saved state. The suspended call is re-invoked and the leaf builtin returns the stored resume value. | `LambdaAsyncFrame` list per task, `mem_calloc` slots, root-registered | FIFO run queue in `LambdaScheduler`, macrotask position |
| Lambda, interp | None. A may-await or task-context procedure is compiled to a MIR satellite when it is defined (`interp.cpp:2544`); an ineligible body sends the whole script to MIR; explicit `interp` rejects it (AI11, AI24, D8.1.1v15). | — | — |
| JS, MIR | Per-function state machine `fn(ctx, env, input, state)` returning a `[value, next_state]` array (`js_mir_function_class_lowering.cpp:1185`). Locals are saved and restored around each suspension (`js_mir_completion.cpp:30,94`). | `JsAsyncFrameCarrier` / `JsGeneratorMapCarrier`, GC-owned | Promise reaction → microtask → `js_async_drive` (`js_runtime.cpp:32070`) |
| JS, interp | Replay. Each resume re-executes the body from the top. A ledger returns recorded results for awaits and yields already passed (`js_interp.cpp:824`); `JsInterpContinuation` cursors (loop, try, list, array-binding) and `JsInterpExpressionReplay` records stop completed work from running twice (`js_interp.cpp:225–290`). | Same carriers plus the `ast_*` replay fields of `JsSuspendedActivation` (`js_runtime_state.hpp:617`) | Same `js_async_drive` |

What is shared already: the libuv loop (`lib/uv_loop.c`), the 32-byte `DurableActivation` base (`durable_activation.hpp`), the promise/handle bridge (`concurrency_js.cpp`), and the phase order "microtask drain, then Lambda `run_ready`" (`uv_loop.c:40`).

### 2.2 Colour in the tiering logic

Tier selection reads function colour in both languages, in opposite directions:

- Lambda forces suspending procedures *into* MIR (`interp.cpp:2544`, `interp_plan.cpp:515,693`).
- JS keeps them *out of* MIR within an interpreted script: promotion rejects `is_async || is_generator` (`js_interp.cpp:6675`) and loop handoff refuses a frame with a suspended activation (`js_interp.cpp:6754`).

### 2.3 Divergences and defects the survey found

These are consequences of having several mechanisms; the design removes the cause of the first four.

- JS MIR splits a module body only at its first top-level `await`. Later ones call `js_await_sync` / `js_p5_module_await`, which busy-drain and can return `undefined` while the promise is still pending (`js_runtime.cpp:31839, 33112`). The interpreter suspends module bodies correctly.
- JS MIR resumable frames have 32 dynamic and 128 spill slots; overflow logs an error and continues (`js_mir_completion.cpp:40`).
- Lambda rejects a value handler whose operand may suspend at compile time (`build_ast.cpp:7748`), because the handler's recovery state cannot cross a state-machine split.
- Lambda MIR pays a re-descent of the whole call chain on every poll.
- `pn_select` has no cancellation check before parking (`concurrency.cpp:1598`).
- `LambdaTask` records are freed only in `lambda_scheduler_destroy`, not when a task completes (`concurrency.cpp:699`).
- `doc/dev/js/JS_09_Async_Modules.md` and `vibe/Lambda_Design_JS_Interpreter.md` describe promise pools and an interpreter that cannot run async code; both are stale.

### 2.4 Measured footprint of the current mechanisms

Lines that mention suspension machinery, counted by identifier on 2026-10-04. These are anchors for §9, not sizes of deletable code.

| Area | File(s) | Lines matching |
|---|---|---|
| JS interp replay, cursors, ledgers | `js_interp.cpp` | 473 |
| JS MIR resumable lowering | `js_mir_*.cpp/.hpp` | ~550 across 12 files |
| JS runtime generator/async drivers | `js_runtime.cpp` | 268 |
| Lambda MIR async transform | `transpile-mir.cpp` | 364 |
| Lambda analysis | `build_ast.cpp` | 42 |
| Lambda interp async routing | `interp.cpp`, `interp_plan.cpp` | 25 |

Whole files in the area: `concurrency.cpp` 1,760; `js_event_loop.cpp` 1,854; `js_mir_completion.cpp` 731; `async.cpp` 568; `concurrency_js.cpp` 209; `lib/uv_loop.c` 147.

---

## 3. Why the stackless family cannot meet the goals

K17 already plans a stackless unification: one shared "resumable function" MIR utility, one frame layout, per-language resume drivers. That plan is sound for the two MIR cases. It does not reach the goals here, for three reasons.

**3.1 A tree-walking interpreter has no stackless suspension that is simple.** An interpreter's activation *is* a chain of C frames. To leave it at an `await` and come back, the walker must either re-execute from the top and suppress everything already done (replay, the JS interpreter's approach), or be rewritten as a non-recursive machine with an explicit frame stack. Replay is the cheaper of the two, and its real cost is visible in `js_interp.cpp`: four continuation kinds, two replay ledgers, expression-level value caches, and special cases for `finally`, destructuring and `for await`. Meeting G2 this way means building the same machinery a second time in `interp.cpp`, where procedures are effectful and the same hazards apply.

**3.2 The line count goes the wrong way.** Extracting a shared MIR utility and merging the runtime records saves on the order of one to two thousand lines. A second interpreter replay implementation adds a comparable amount. G4 is at best a wash.

**3.3 Colour stays in the compiler.** Under any stackless scheme the set of functions to transform is the may-await closure, the calling convention has two shapes (`value | suspended` and plain), and indirect `pn` calls are transformed conservatively. Tiering can be made colour-blind only by making every tier implement every colour, which is exactly the duplication above.

The stackless family therefore gives one *protocol* with four *implementations*. The user goal is one implementation.

---

## 4. The design: one stackful Activation

### 4.1 K2-R revisited

K2-R (`Lambda_Design_Concurrency.md` §4.2.3–§4.2.5) chose state machines over fibers on four factors. Two were decisive; neither holds today.

| K2-R factor | Then (2026-07-08) | Now |
|---|---|---|
| 4. GC interaction — "the decisive row" | A parked fiber stack is a GC root; v1 would scan it conservatively, and level 1 would be gated on G1. | Conservative native-stack scanning is retired (CLAUDE rule 15). Under D5.1.1v2 the native C stack is invisible to the collector and every live Item is on the precisely scanned root stack. A parked native stack needs no scanning at all; only its root-stack segment does, and that is an exact `[base, top)` range. |
| 2. "Awaits never occur inside C frames" | True for MIR-compiled Lambda. | False for the AST interpreter, which did not exist as T0 until D8.1.1v2 (2026-08-15). Interpreted code is C frames all the way down; this is the reason AI11 had to send suspending procedures to MIR. |
| 3. "The machinery half-exists" | One JS lowering existed. | Two lowerings and one replay engine exist, and they diverge (§2.3). The machinery is now the cost. |
| 1. Closed world | The transform is one more whole-program analysis. | Still true, and no longer needed. |

K2-R recorded the fiber design as "superseded but viable … revived if the transform proves worse than expected" (§4.2.4). The interpreter tier is the case it did not anticipate.

**RA1.** Suspension is stackful. An activation owns a native stack; suspending is a context switch. No tier transforms a function in order to suspend it.

### 4.2 The Activation

One record replaces `LambdaAsyncFrame`, the suspension half of `JsSuspendedActivation`, `JsGeneratorStateRecord`'s resume state, and `JsAsyncContextStateRecord`'s resume state.

```c
typedef enum { ACT_NEW, ACT_RUNNING, ACT_SUSPENDED, ACT_DONE } ActivationStatus;

typedef struct Activation Activation;            // GC-owned carrier (D5.1.1)
typedef Item (*ActivationEntry)(Activation* self, Item arg);

Activation*      activation_create(ActivationEntry entry, Item arg);
ActivationStatus activation_resume(Activation* a, Item input);  // runs until it suspends or returns
Item             activation_value(Activation* a);               // yielded value, or the result when DONE
Item             activation_suspend(Item value);                // called inside; returns the next resume input
Activation*      activation_current(void);                      // NULL on the context's base stack
void             activation_abandon(Activation* a);             // finalizer and teardown path (§4.6)
Item             activation_call_on_base(Item (*fn)(void*), void* arg);  // run stack-hungry native work off the fiber
```

`activation_suspend` may be called at any depth, from interpreted code, from JIT code, or from a runtime helper that either tier calls. That one property is what makes the four cases one.

**RA2.** The whole suspension ABI is the seven functions above. Both tiers of both languages call the same ones.

### 4.3 Stacks, side stacks and the collector

D5.1.1v2 names three stack-like mechanisms: the native C stack, the watermarked side stacks (root and number), and heap async frames. An activation gets its own instance of the first two, and the third disappears.

- **Native stack.** One reservation per activation, committed lazily, with a guard page. The collector never looks at it.
- **Root-stack segment.** The activation's own `[base, top)` range. It is the activation's Item region in the sense of D5.1.1v2: the collector traces it *through the owner* (the carrier), exactly as it traces a JS environment today. No root-range registration.
- **Number-stack segment.** The activation's own range, never scanned.

Because each activation's segments are at fixed addresses for its lifetime, interior pointers held by frames (`InterpFrame::slots`, `Rooted<>` homes, MIR frame slots) stay valid across a park. Nothing is copied or re-homed, which keeps D5.1.3 intact.

Stacks are pooled. An activation that returns hands its stack back; a stack that returns to the pool keeps its mapping and is decommitted by the existing `lambda_side_stack_decommit_unused` policy.

**RA3.** A parked activation contributes exactly one root range to a collection: its root-stack segment, traced through the carrier. Parked native stacks and number segments are never scanned.

**RA4.** No conservative scanning is introduced anywhere (CLAUDE rule 15).

### 4.4 The switch and ambient state

The switch saves callee-saved registers and the stack pointer; it is hand-written per ABI (x86-64 SysV, AArch64, x64 Windows), as K2 §4.2.4 already specified, and explicitly not `ucontext`. It lives in `lib/` as first-party code.

Everything that is thread-local *and* LIFO with the call stack must switch with it. This is one struct, saved and restored in one place:

- side-stack watermarks: root base/top/limit, number base/top/limit;
- the recoverable stack limit used by overflow checks (`lambda-stack.h`);
- the recovery-frame chain head (D5.3.6);
- the scheduler's current task;
- the interpreter frame chain heads for both interpreters;
- JS ambient state: `js_current_this`, the `with` chain, the active module state id, the async-hooks current resource.

Today `js_async_drive` and `lambda_scheduler_run_one` save and restore parts of this by hand. They stop doing so.

**RA5.** Ambient per-activation state is one struct swapped by the switch. Drivers contain no save/restore code.

**RA6.** Stack-hungry native work that cannot suspend (a MIR compile triggered by promotion, a parser run for `eval`) executes through `activation_call_on_base`, so activation stacks can be small.

### 4.5 Recovery frames

D6.3.3 says recovery frames never survive a scheduler yield, and D5.3.6 makes them TLS-LIFO per boundary. With one chain per activation both stay true in the sense that matters: a jump never crosses a stack, and each chain is LIFO. What changes is that a frame may now remain armed across a park, because the stack it lives on is preserved.

This removes the Lambda async-frame fault routing (`async_route_fault`, `fault_target_state`, the handler state emission around `transpile-mir.cpp:22027–22190`): a statement handler in a suspending procedure is an ordinary handler. Whether to lift the compile-time rejection of value handlers with suspending operands (§2.3) is a separate semantics question and is not decided here (RA-O6).

### 4.6 Abandonment

A parked activation can become unreachable (a generator that is dropped, an async function awaiting a promise that never settles, a scheduler destroyed with parked tasks). Its native frames are then discarded without running.

- No user code runs on abandonment. This matches JS, where a collected generator does not run `finally`.
- A native frame that can be live across a suspension point must not own heap memory that only its own unwinding would free. Such memory moves to the activation (an arena freed with it) or to GC ownership. This is the stackful form of the rule the replay engine already follows through `js_generator_map_heap_destroy`.
- Structured cancellation (S13, `lambda_task_cancel`) is unaffected: it is a normal resume with an error value, and the task unwinds by returning.

**RA7.** Abandonment frees the stack and the activation arena and runs nothing. The audit of native frames that hold owned memory across a possible suspension is a P0 deliverable (RA-O2).

---

## 5. One engine

### 5.1 Records

```
Activation        suspension only: stack, segments, status, value slot, ambient
  └─ Task         scheduling: state, park kind, handle, result, lazy mailbox, scope, waiters, cancel flag
```

- A **Lambda task** is a Task whose activation runs the started procedure. There is one activation per task, not one frame per call: `pn` calls inside a task are plain calls.
- A **JS async function call** is a Task with a lazy mailbox, weakly registered with the scheduler, whose handle is its Promise. This is JSCU25 as ratified.
- A **JS generator** is a bare Activation driven synchronously by `next`/`return`/`throw`. It is not a task.
- A **JS async generator** is a Task whose activation both awaits and yields.
- A **module with top-level await** is a Task whose activation runs the module body.

`LambdaTask` loses `resume`, `frame`, `frame_roots`, `async_frames`, `async_cursor`, `resume_value`, `has_resume_value` and the launch frame. `JsSuspendedActivation` loses `state_fn`, `state`, and every `ast_*` replay and continuation field.

**RA8.** Task is the only scheduling record and Activation the only suspension record, for both languages.

### 5.2 Queue, loop and ordering

- One `RuntimeJobQueue` (`async.cpp`) with lanes: nextTick, microtask, task, animation frame. The Lambda FIFO run queue becomes the task lane.
- One drain routine replaces `lambda_scheduler_drain` and `js_event_loop_drain`, with one watchdog.
- Ordering is D6.3.1, unchanged: a Lambda resume is a macrotask in FIFO readiness order and never interrupts a JS job or its microtask checkpoint. A JS activation is resumed by its reaction job on the microtask lane; the task lane never resumes a JS activation.

**RA9.** Readiness policy is the only per-language difference in the scheduler: microtask lane for JS activations, task lane for Lambda tasks.

### 5.3 Language surfaces on the engine

**Lambda.**
- `wait`, `receive`, `select`, `sleep`, `io_read`: set the park kind, call `activation_suspend`, return the resume input. The re-invoke-and-`take_resume_value` protocol is deleted.
- `start`: create a Task and enqueue it. Both tiers call `lambda_task_start_function_scoped`; T0 gains an `AST_NODE_START` evaluator.
- Scopes: `lambda_task_scope_enter` / `leave` are called by both tiers at blocks that contain a non-escaping `start`. Joining children is a plain loop of parks.
- A procedure's root invocation outside any task runs as the root task, as `lambda_task_run_root_function` does today.
- Calling convention: there is one. `ITEM_TASK_SUSPENDED` and the `value | suspended` convention are deleted.

**JavaScript.**
- `await v`: `PromiseResolve(v)`, then `activation_suspend(promise)`. The driver registers the two reactions bound to the carrier, as now; the reaction job resumes the activation with the value or a throw signal. Job order is exactly today's.
- `yield v`: `activation_suspend(v)`. `next(x)` is `activation_resume(a, x)`. `return` and `throw` resume with the existing signal values, decoded at the yield site as now.
- `yield*`: an ordinary loop inside the generator's activation. The delegate state (`delegate`, `delegate_resume`, `delegate_idx`) and its drivers are deleted.
- An async function with no `await` in its body keeps the existing direct path (`js_async_wrap_return`) and creates no activation.
- Top-level await is a normal await in both tiers. `js_await_sync`, `js_await_sync_incremental`, `js_p5_module_await` and the single-shot module split are deleted.

**The membrane** (K10 layer 3) is unchanged in meaning: a may-await `pn` invoked from JS is dispatched as a fresh task and returns a Promise. The `may_await` fact is still computed, but only as an interface fact for this wrapper and for diagnostics. It no longer selects a compilation strategy (D6.1.3).

**Native frames.** D7.4.2 shields modules from the async substrate, and §4.4 of the concurrency design forbids suspending with a host-API frame on the stack. Under state machines that was impossible by construction; here it is enforced by a per-activation barrier depth that `activation_suspend` checks. A violation is a fault, not undefined behaviour.

**RA10.** A suspension attempted with a native barrier on the activation's stack, or with no current activation, is a reported fault.

---

## 6. The four cases after the change

| Case | Suspends by | What the tier itself contains |
|---|---|---|
| Lambda, interp | `activation_suspend` inside the task builtins | A `start` evaluator and scope calls. Nothing else. |
| Lambda, MIR | same | Calls to the same builtins. No prologue dispatch, no spills, no resume labels. |
| JS, interp | `activation_suspend` at `await` / `yield` nodes | Two node evaluators that call the helper. No replay, no cursors, no ledgers. |
| JS, MIR | same | A call instruction at each `await` / `yield`. No state function, no env layout, no save/restore. |

**Tiering (G3).** A function's tier is the entry its callable currently points at. An activation runs whatever its callee entries are at the time; a function can be interpreted in one activation and compiled in the next, and loop handoff inside a live activation is an ordinary call on the same stack. The following are deleted:

- Lambda: satellite publication at definition for may-await procedures (`interp.cpp:2544–2562`), `interp_async_proc_satellite_supported` and the whole-script fallback it triggers, and the task-backed rejection in strict `interp` (the async clause of D8.1.1v15 / AI24).
- JS: the `is_async || is_generator` promotion rejection and the suspended-activation handoff refusal.

**RA11.** No tier-selection code reads function colour. Hit counters are the only input.

**RA12.** AI11 is retired: T0 executes suspending definitions.

---

## 7. What does not change

- JS observable behaviour: job ordering, run-to-completion, promise resolution, generator protocol. test262 is the gate.
- D6.3.1 ordering and the K13 capture rule.
- S13 handle, mailbox and failure semantics; S7.11 fault semantics.
- K7: tasks never migrate between threads. K31 worker isolates remain the multicore road; each isolate has its own scheduler and its own activation pool.
- `JsPromise`. It is still the model for a completable.
- The libuv services (timers, fs, net) and their two fronts.

---

## 8. Costs and risks

**8.1 Parked memory (the main cost).** A state-machine frame is a few hundred bytes. A parked activation holds at least one committed page each for its native stack, root segment and number segment: about 12 KB with 4 KB pages and 48 KB with the 16 KB pages of Apple Silicon. A thousand simultaneously pending JS activations is tens of megabytes. K10 layer 2 gave this as a reason not to move JS to fibers, and it is still the strongest objection. Mitigations, in order of preference:

1. Pooling makes activations that complete promptly free; only *parked* ones hold pages.
2. Root and number segments can share one committed page, growing toward each other, if D5.1.1's "two strictly separate mappings" is read as "two disjoint, exactly bounded ranges" (RA-O3).
3. Cold-park compaction: copy the used bytes of a long-parked activation to a heap blob and decommit its pages, restoring to the same addresses on resume. Parked cost then approaches the stackless figure at the price of a copy (RA-O4). Not in the first version.
4. Fallback: keep the JS MIR state machines as a second Activation kind (§10, gate G-P3).

**8.2 Stack depth inside an activation.** The main stack budget is 32 MB (`LAMBDA_STACK_DEFAULT_BUDGET`). Activation stacks will reserve less, so deep recursion inside a task or an async function overflows sooner than at top level. Reservation is virtual and cheap on 64-bit, so the size is a tuning question, but the difference is observable (RA-O5).

**8.3 Platform surface.** Three hand-written switches, plus shadow-stack (CET) and sanitizer annotations. This is small code but it is the least forgiving code in the runtime.

**8.4 Ambient-state audit.** Any TLS LIFO state missed in §4.4 becomes a cross-activation corruption bug. This is the same kind of inventory as the per-isolate state audit in K31, and should share its list.

**8.5 Stage B.** `Lambda_Design_Concurrency.md` §10.5 kept state machines partly because a parked state machine can resume on any pool thread. Stackful activations are pinned to their context's thread. K7 already forbids migration and K31 already made worker isolates the primary multicore story, so this forfeits only the deferred Stage-B optimisation. It is still a real narrowing and needs to be accepted explicitly.

**8.6 WebAssembly.** `build_lambda_config.json` still names a wasm entry (`lambda/lambda-wasm-main.c`, last touched 2026-08-06) and `make build-wasm` refers to a `compile-wasm.sh` that is not in the tree. WebAssembly has no native stack switching, so a wasm build would need Asyncify or JSPI for this design. If wasm is a live target, that must be weighed before ratification (RA-O1).

**8.7 Debugging.** This one improves: a debugger shows a real stack for a suspended task instead of a chopped one.

---

## 9. LOC ledger (estimates)

Ranges are derived from the identifier counts in §2.4 and from the sizes of the regions named in §2.1. They are planning figures; each phase reports its measured diff.

| Change | Estimate |
|---|---|
| JS interp: replay ledgers, continuation cursors, expression replay, resume entry points | −1,600 to −2,200 |
| JS MIR: resumable state-machine lowering, env save/restore, yield/await lowering, module split, analysis | −1,200 to −1,800 |
| JS runtime: `[value, state]` protocol, delegate machinery, driver kind dispatch, `js_await_sync` family | −700 to −1,100 |
| Lambda MIR: async helpers, prologue dispatch, spills, resume points, fault routing | −900 to −1,300 |
| `concurrency.cpp`: async frames, launch/re-descent, resume-value protocol | −350 to −500 |
| Lambda interp and plan: async satellite routing and eligibility scans; `build_ast` await-point bookkeeping | −150 to −300 |
| Loop: two drains and watchdogs into one, run queue into a lane | −150 to −300 |
| **Removed** | **−5,050 to −7,500** |
| Activation core, stack pool, side-stack segments | +450 to +600 |
| Context switch, three ABIs | +120 to +150 |
| Ambient swap, recovery and stack-limit integration | +150 to +250 |
| Lambda interp `start` and scopes | +80 to +150 |
| JS await/yield helpers, both tiers | +150 to +250 |
| Abandonment and activation arena | +100 to +200 |
| **Added** | **+1,050 to +1,600** |
| **Net** | **about −4,000 to −6,000** |

If gate G-P3 fails and the JS MIR state machines stay, the JS MIR row and about half of the JS runtime row are not removed, and the net falls to roughly −2,000 to −3,000. G4 holds either way.

CLAUDE rule 19 applies: reductions come from deleting mechanisms, never from removing comments or reformatting.

---

## 10. Phases and gates

Each phase is shippable and ends green.

**P0 — Preflight (no behaviour change).**
- Context switch and stack pool in `lib/`, with unit tests.
- Per-activation side-stack segments; collector traces a parked segment through its carrier.
- The ambient-state inventory (§4.4) and the owned-memory audit (§4.6).
- Gate: new gtests; `make test-mir-gc-stress` and `make test-gc-rooting` with parked activations under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; baselines unchanged.

**P1 — Lambda, both tiers.**
- Task owns an Activation. Task builtins park through `activation_suspend`.
- T0 evaluates `start` and scopes; the async satellite path and eligibility scans are deleted.
- The Lambda MIR state-machine transform, `LambdaAsyncFrame` and `ITEM_TASK_SUSPENDED` are deleted.
- Folded-in fixes: `pn_select` cancellation check; task records freed at completion.
- Gate: `make test-lambda-baseline` at 100% under `LAMBDA_TIER=interp` and `LAMBDA_TIER=jit`; the concurrency goldens identical in both; GC stress.

**P2 — JS interpreter.**
- `await` and `yield` evaluators call `activation_suspend`. Replay, cursors and ledgers are deleted. The driver keeps its MIR state-function branch for now.
- Gate: test262 baseline with 0 regressions; JS gtest; node baseline.

**P3 — Measurement gate (G-P3), release build only (CLAUDE rule 10).**
- Parked memory at 1,000 and 10,000 pending async activations and at 5,000 live generators.
- Switch cost against the current MIR state machine on generator-heavy and await-heavy benchmarks.
- Outcome decides P4: retire the JS MIR state machines (default), or keep them as a second Activation kind.

**P4 — JS MIR.**
- `await` and `yield` lower to helper calls. The resumable lowering, the `[value, state]` protocol and the delegate machinery are deleted. Module top-level await becomes a normal await.
- Colour predicates in JS promotion and handoff are deleted.
- Gate: test262 baseline with 0 regressions, and the currently failing top-level-await cases re-examined.

**P5 — Engine merge.**
- One job queue with lanes, one drain, one watchdog. JS activations become weakly registered tasks (JSCU25).
- Gate: Lambda and Radiant baselines; `RuntimeGlobalsConcurrency`; the JSCU25 acceptance cases in `Lambda_Design_Structs_JS.md` §1.5.

**P6 — Documents.** After ratification: revise the formal specs (§11), and refresh `doc/dev/js/JS_08`, `JS_09`, `doc/dev/lambda/LR_12` and `doc/Lambda_Concurrency.md`.

P1 comes before the JS work because Lambda's async test surface is the smaller one and the Lambda interpreter is where the goal is unmet. P2 comes before P4 because the interpreter replay machinery is the larger deletion and carries no measurement risk.

---

## 11. Rulings this design would revise — each needs a user ruling

Nothing here is changed until ruled. On ratification, each revised ruling is updated in place in the formal spec with a version suffix and a document semver bump, together with the vibe record named (CLAUDE rule 17).

| Ruling | Today | Proposed |
|---|---|---|
| **K2-R** (`Lambda_Design_Concurrency.md` §4.2.3, §10.5) | May-await state machines; fibers superseded | Stackful activations; state machines retired. The fiber reference design of §4.2.4 becomes the adopted one, on precise side stacks. |
| **K17** (§10.7) | Unify layers 1, 2, 5; keep 3, 4 per language | Layers 1, 2 and 4 cease to exist (no transform, no frame, one calling convention). Layer 3 stays per language (RA9). Layer 5 as is. |
| **K10 layer 2** (§4.6) | JS stays on its state machines | JS semantics unchanged; its suspension mechanism moves to activations, subject to G-P3. |
| **D5.1.1v2** | Third mechanism is "heap async frames", a tail-bearing container traced through its owner | Third mechanism is "activation stacks": a native stack and side-stack segments per activation; the root segment is the Item region traced through the owner; native stack and number segment never scanned. |
| **D5.3.6**, **D6.3.3** | Recovery frames TLS-LIFO; never survive a scheduler yield | One LIFO chain per activation; a frame may stay armed across a park on its own stack; no jump crosses a stack. |
| **D6.1.3** | `may_await` analysis (drives the transform) | `may_await` is an interface fact for the JS membrane and diagnostics only. |
| **D8.1.1v15**, **AI11**, **AI24** | Suspending definitions bypass T0; strict `interp` rejects task-backed bodies | T0 executes them; the rejection clause is removed; promotion ignores colour (RA11, RA12). |
| **JSCU26**, **JSCU32** | Move `LambdaAsyncFrame` onto the JS env carrier; shared `JsSuspendedActivation` prefix | Superseded: there is no Lambda async frame, and the shared prefix reduces to the Activation. JSCU25 stands. |
| **Stage B** (§10.6) | Deferred M:N of parked state machines over a thread pool | Withdrawn as a road. K7 and K31 stand. |
| **D7.4.2** and concurrency §4.4 | No suspension under a host-API frame, true by construction | Same rule, enforced at run time (RA10). |

Unchanged and relied on: D5.1.3, D5.3.2, D6.2.2v2, D6.3.1, S7.11, S13.1–S13.2, K3, K7, K13, K31.

---

## 12. Decision ledger

| ID | Decision | Status |
|---|---|---|
| RA1 | Suspension is stackful; no tier transforms a function in order to suspend it | proposed |
| RA2 | The suspension ABI is the seven `activation_*` functions, shared by all four cases | proposed |
| RA3 | A parked activation contributes one exact root range, traced through its carrier | proposed |
| RA4 | No conservative scanning is introduced | proposed |
| RA5 | Ambient per-activation state is one struct swapped by the switch | proposed |
| RA6 | Non-suspending stack-hungry native work runs on the base stack | proposed |
| RA7 | Abandonment frees the stack and arena and runs no code | proposed |
| RA8 | Task is the only scheduling record, Activation the only suspension record | proposed |
| RA9 | Readiness lane is the only per-language scheduler difference | proposed |
| RA10 | Suspending under a native barrier, or with no activation, is a reported fault | proposed |
| RA11 | Tier selection reads hit counters only, never function colour | proposed |
| RA12 | AI11 retired: T0 executes suspending definitions | proposed |

## 13. Open items

| ID | Question |
|---|---|
| RA-O1 | Is WebAssembly a live target? If so, choose Asyncify or JSPI, or keep a stackless path for that build. |
| RA-O2 | Inventory of native frames that own heap memory across a possible suspension (both interpreters, runtime helpers that call back into script). |
| RA-O3 | May root and number segments share one mapping with an exact watermark between them, to halve the minimum parked footprint? |
| RA-O4 | Cold-park compaction: thresholds, and whether it is needed at all after G-P3 measurements. |
| RA-O5 | Activation stack reservation size, and whether stack-overflow depth inside an activation should match the top-level budget. |
| RA-O6 | With handlers able to stay armed across a park, should the compile-time rejection of a value handler with a suspending operand be lifted? A semantics question; no formal ruling located yet. |
| RA-O7 | Should Lambda task completion and JS promise settlement share one waiter/reaction list, shrinking `concurrency_js.cpp` further? Deferred until after P5. |
| RA-O8 | Whether MIR-generated code caches a side-stack watermark in a register across calls; to be checked in P0 before segments are swapped under it. |

---

## 14. Comparisons

### 14.1 Stackful Activation against the current implementation

An Activation is a fiber: the design of `Lambda_Design_Concurrency.md` §4.2.4, with the addition that each fiber owns its own root and number side-stack segments, so the collector never scans a native stack (RA3, RA4).

**What the change implies**

- Suspension stops being a compiler concern. Any code can suspend at any call depth in either tier; functions are not split and interpreters do not replay.
- Lambda gets one fiber per task, not per call. `pn` calls inside a task are plain calls; the `value | suspended` convention and the per-poll re-descent are gone.
- JS gets one fiber per pending async call and per live generator, because each of those suspends independently of its caller.
- Thread-local, stack-shaped state must switch with the fiber (§4.4). A missed item is a cross-activation corruption bug.
- Native frames can be abandoned without unwinding (§4.6), so they must not own memory that only their own return would free.
- A task is pinned to its thread for life (§8.5).

**Side by side**

| | Current (stackless) | Stackful Activation |
|---|---|---|
| Mechanisms | Three, plus one case that cannot suspend (§2.1) | One |
| Lambda interp async | Needs a MIR satellite (AI11) | Direct |
| Tiering | Reads function colour, in opposite directions per language (§2.2) | Hit rate only (RA11) |
| Parked cost | A few hundred bytes | About 12 KB with 4 KB pages; about 48 KB on Apple Silicon (§8.1) |
| Lambda resume cost | Re-descends the whole call chain on every poll | One switch |
| JS suspend/resume cost | A function call plus slot saves | A register switch; expected comparable, unmeasured (G-P3) |
| Fixed limits | 128 spill slots in JS MIR; MIR top-level await splits once (§2.3) | None of these |
| Platform code | None | Hand-written switch for three ABIs (§8.3) |
| Debugger view of a parked task | Chopped stack | Real stack |
| Recursion depth inside a task | Main-stack budget, 32 MB | The activation's reservation (RA-O5) |
| Cross-thread resume | Possible in principle (Stage B) | Not possible |
| WebAssembly | Works | Needs Asyncify or JSPI (RA-O1) |
| Code size | Baseline | About −4,000 to −6,000 lines, estimated (§9) |

**Pros.** One place where suspension happens; G1–G5 met; the divergences of §2.3 removed at the cause; less code.

**Cons.** Parked memory for JS workloads with many pending promises or live generators; low-level per-platform switch code; the ambient-state and abandonment audits; cross-thread task scheduling and easy wasm given up.

### 14.2 Against goroutines

A goroutine is the same concept: a stackful unit that parks at blocking points, with the colour hidden by the runtime. `Lambda_Design_Concurrency.md` §2.1 already records which parts of Go's machinery Lambda takes. The differences that matter here:

| | Goroutine | Activation |
|---|---|---|
| Stack | Starts near 2 KB and grows by copying the stack and rewriting every pointer into it; needs compiler-emitted precise stack maps | Fixed reservation, lazily committed, never moves, because C and MIR frames hold interior pointers that cannot be rewritten |
| Memory floor | Kilobytes; millions of goroutines are routine | Pages; the target is thousands |
| Scheduling | M:N over OS threads with work stealing | One thread per context; a task never migrates (K7) |
| Preemption | Asynchronous, signal-based | None; cooperative at explicit suspension points only |
| Parallelism | Goroutines run in parallel on a shared heap | Worker isolates with separate heaps (K31), the BEAM shape rather than Go's |
| Collector and stacks | Scans goroutine stacks precisely through the stack maps | Never scans a native stack; live values are on the separate root stack, traced as one exact range per activation |
| Blocking foreign calls | The runtime hands the thread off around cgo and syscalls | Not allowed to suspend under a host-API frame (RA10); blocking host calls pin the context |

In short, the design takes Go's programming model and leaves out its growable stacks, its multi-threaded scheduler and its preemption. Leaving out growable stacks is what sets the per-activation memory floor; leaving out the other two is what keeps the allocator and collector single-threaded per context.

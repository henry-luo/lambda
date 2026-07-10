# Lambda JS Threading Design — One Context, One Thread

**Status:** proposal v2 (2026-07-10) — extends the v1 batch-stack proposal into one threading model for every JS entry point.
**Context:** applies the Lambda concurrency design v3 (`Lambda_Design_Concurrency.md` — K3/K10 one loop per context, §5.5 isolate topology, K30c safepoints) and the Radiant concurrency design (`radiant/Radiant_Design_Concurrency.md` — RC1/RC2/RC4/RC7) to the question "which thread runs JS?". The goal: each near-term fix is a down-payment on the RC2 page-isolate topology, not a workaround to unwind later.

---

## 1. The execution surface today (verified 2026-07-10)

Four JS entry points, three different threading shapes:

| Entry point | Executes on | Stack | uv loop | Crash / timeout guard |
|---|---|---|---|---|
| `lambda.exe script.js` (node-like) | dedicated pthread, joined by main (`js_cli_transpile_with_execution_stack`, `main.cpp:189`) | **256 MB** | init, drain, shutdown all on that thread | `lambda_stack_init()` per thread |
| `lambda.exe js-test-batch` | **process main thread** (`main.cpp:4057`) | process default (macOS ~8 MB) | per script | process-global `sigsetjmp` state (`batch_crash_jmp`, `main.cpp:1331`) + `alarm()`/SIGALRM |
| `layout` / `render` / `view` page JS | process main thread (`radiant/script_runner.cpp:2026`; view loop `window.cpp:1289`; handlers re-entered from `event.cpp`, loop pumped via `js_event_loop_pump_nowait`) | process default | pumped from the UI loop | own SIGSEGV/SIGBUS + SIGALRM guard, file-static state (`script_runner.cpp:65`) |
| JS gtests | subprocess running `js-test-batch` (`test_js_gtest.cpp:390`) | inherits batch | — | inherits batch |

Also load-bearing:

- The JS CLI in HTML/DOM mode deliberately **bypasses** the execution-stack thread and runs on main (`main.cpp:2395`) — DOM/UI-coupled JS is main-thread today by design, not accident.
- Module **compilation** is already fork-join parallel (compile workers, serial execute — `js_mir_module_batch_lowering.cpp:7898`): compilation threading and execution threading are separable concerns; this doc governs *execution*.
- The runtime is already context-per-thread-shaped: `context` is `__thread` (`runner.cpp:296`), and the lambda-stack overflow guard keeps all its recovery state `__thread` (`lambda-stack.h`).
- The uv loop is currently a **single process-global** (`g_loop`, `lib/uv_loop.c:11`) pumped by whichever thread is the executor; timers are uv handles, promise jobs are static ring buffers flushed as libuv's microtask checkpoint (`js_event_loop.cpp`). Workable because at most one JS engine executes at a time — see §6.5.
- In **view** mode the loop is host-driven (`js_dom_is_host_driven_loop()`): load-time drains flush microtasks only; timers/rAF are deferred past the first layout commit and pumped by the GLFW loop (`uv_run(UV_RUN_NOWAIT)` + `js_animation_frame_flush`, `window.cpp:1289–1301`) — browser-correct cooperative interleaving of JS slices with layout/paint on one thread.

### The symptom that motivated v1

Modern JS workloads are stack-hungry (deep recursion like `test/js/tco.js`; generated code like Ajv's `new Function` validators; debug-build MIR/JIT frames). `tco.js` and `lib_ajv.js` pass on the 256 MB CLI path and in focused gtests, but crash **batch-only** — the scripts aren't broken; the batch entry point simply runs them on a different, smaller stack. The remaining issue is entry-point inconsistency.

---

## 2. The invariant — the context-thread rule (JT1)

v1 stated: *"all non-UI JavaScript execution entrypoints run on the JS execution stack."* Right direction, but framed as an exception list ("non-UI"). The form that covers all four entry points and matches the concurrency ledgers:

> **Every JS/Lambda context executes on a thread that the context owns — its *context thread*. The process main thread is the shell: it spawns workers, waits, routes OS/UI events and messages; it never executes script.**

Why this is the right generalization:

- It is **RC2 stated one level down**: "page = isolate: one Lambda context on one thread" and §5.5's "an isolate = a context on its own thread". The CLI's 256 MB helper thread is the degenerate single-isolate case (shell = a trivial joiner); the batch worker is "one context thread, many successive hot contexts"; a Radiant page thread is the same shape again.
- **UI is not an exception — it is the reason the rule has this shape.** macOS requires the UI event loop on the main thread, so when RC2's page isolates land, page JS *must* leave the main thread anyway. In the end state, *no* mode executes JS on main; today's view-mode main-thread JS is the documented interim (JT7), not a carve-out.
- The stack budget becomes a property of the **JS runtime boundary** (the context thread), not of the whole process — v1's "why not set the main thread stack" argument (macOS fixes the main stack at load time; Linux depends on inherited `ulimit -s`; Windows is linker `/STACK`), kept verbatim.

### Per-mode application

1. **CLI js** — already conformant; keep as is.
2. **js-test-batch (JT2 = v1 proposal, adopted unchanged):** the *whole* batch loop moves onto one worker pthread with the same 256 MB stack — stdin read/parse, source-blob reads, harness/preamble handling, compile, execute, reset, and the `BATCH_*` protocol writes. One process, one batch worker thread, one stdin/stdout loop, hot heap/preamble reuse, existing per-script crash recovery — all inside that worker. **No thread-per-script**: hot-context reuse is the batch's performance model, and one long-lived thread preserves it.
3. **layout/render/view** — interim: page JS stays on the main thread under the *browser* stack profile (§3); end state: the RC2 page thread. **Do not give page JS a 256 MB helper thread**: it would put JS on a thread the page context doesn't own (fighting RC2), and it's the wrong semantics anyway — browsers deliberately give page JS a small, deterministic stack and throw `RangeError` (a runaway page script must not get 256 MB of real recursion before failing).
4. **JS gtests** — inherit (2) for free via the subprocess boundary.

---

## 3. Stack model — budget ≠ limit (JT3)

256 MB fixes the inconsistency, but it is an engineering **budget**, not the semantic answer. Decouple two numbers:

- **Native stack size** — per-context-thread, set by the embedding profile below.
- **JS-visible recursion limit** — enforced by the existing lambda-stack guard (fault-address disambiguation → `siglongjmp` → runtime error), sized *below* the native budget so recovery always has headroom.

**The `RangeError` conversion already exists** — but only one entry point gets it. The CLI path arms the thread-local recovery point around `js_main` and converts a guard-page fault into `js_throw_range_error("Maximum call stack size exceeded")` (`js_mir_entrypoints_require.cpp:962–978`). Batch and Radiant then install their **own** SIGSEGV handlers over the lambda-stack one (`main.cpp:4144`; `script_runner.cpp:90`) — process-global disposition, last install wins — so on those paths a deep-recursion overflow degrades to a generic caught crash (exit 139) instead of a clean `RangeError`. That is the concrete mechanism behind "tco.js crashes batch-only": smaller stack **and** a coarser handler. The fix is not new plumbing but handler unification (JT4b).

| Profile | Used by | Native stack | Overflow semantics |
|---|---|---|---|
| **node** | CLI js, js-test-batch | 256 MB | `RangeError` — live on CLI today; batch gets it via JT4b (currently degrades to exit-139 crash recovery) |
| **browser** | page JS in layout/render/view | thread default now; modest (e.g. 32 MB) when RC2 page threads exist | `RangeError` into the page; the page survives — RC7 crash containment in miniature |

This converts today's CLI-vs-view stack difference from an inconsistency into an **intended profile difference**, matching the Node-vs-browser reality JS developers already know. (Budgets are larger than V8's ~1 MB JS stack because MIR-compiled JS frames are full native frames, plus debug-build helper depth.)

v1's closing note is kept and sharpened: the threading change is the consistency fix; guard-limit-based `RangeError` is the hardening, and the mechanism for it (lambda-stack) already exists and is already per-thread.

---

## 4. Caveat 1 — crash-recovery signals (JT4)

v1's analysis is correct: SIGSEGV/SIGBUS are synchronous, thread-directed faults — the batch worker faults, the handler runs on the batch worker, `siglongjmp` recovers inside the batch worker. Main never participates. Sharpened into a rule that stays correct through RC2:

> **Install signal handlers once per process; arm recovery state per thread.**

- `sigaction` dispositions are process-global — the batch already installs once before the loop (`main.cpp:4136`); that stays. What must change is *whose* state the handler consults:
- `batch_crash_jmp` / `batch_crash_active` / `batch_timeout_jmp` / `mir_error_jmp` (`main.cpp:1308–1331`) and Radiant's `js_exec_jmpbuf` / `js_exec_guarded` (`script_runner.cpp:65`) are **plain statics**. Safe while exactly one thread executes script; they become `__thread` as part of this change (cheap now), so that when multiple executing threads exist (page isolates), a crashing page can never `siglongjmp` through another page's buffer. lambda-stack already models this correctly (`_lambda_recovery_point`, `_lambda_recovery_armed` — all `__thread`).
- `sigaltstack` is **per-thread** and must be installed *on* the worker; the current `static char alt_stack_mem[128K]` (`main.cpp:4102`) is single-executor-only and becomes per-thread storage. Without a per-thread altstack, a stack-overflow SIGSEGV on the worker has no stack to run its handler on — a double fault, i.e. exactly the crash class this design exists to contain.
- **Consolidation (JT4b):** the batch crash guard, Radiant's `js_exec` guard, and lambda-stack are three implementations of one mechanism (armed flag + jmp buffer + altstack + handler) that today **fight over the same process-global SIGSEGV disposition — last install wins**, which is how batch/Radiant currently disable the CLI path's overflow→`RangeError` conversion (§3). Fold them into one process handler with a thread-local dispatch chain: (1) stack-overflow fault-address check → thread-local recovery → `RangeError` (lambda-stack's logic, first because it is the most precise); (2) otherwise the embedding's crash-catcher recovery point (batch per-script recovery / Radiant page guard) if armed; (3) otherwise default disposition. One recovery kit, three users, and overflow behaves identically at every entry point. This is also the prerequisite for RC7 (page crash = `T^E` at the shell) — that feature is precisely "this recovery kit, reported over a handle".

---

## 5. Caveat 2 — timeouts (JT5): thread-directed delivery, not mask choreography

v1 offered two options: (a) mask SIGALRM in main / unblock in the worker / handle in the worker; (b) longer-term, replace `alarm()`. Recommendation: **skip (a) — it has a real hole — and do a small version of (b) now.**

**The hole in (a): libuv's threadpool.** Signal masks are inherited from the *creating* thread. libuv spawns its worker pool lazily, from whichever thread first requests pool work — that is the JS thread, where SIGALRM is deliberately *unblocked*. Every uv pool thread then inherits the unblocked mask, and a process-directed SIGALRM may be delivered to a uv thread whose stack has no recovery point — `siglongjmp` from there is undefined behavior across threads. Keeping (a) sound requires masking around uv-pool spawn and inside every future thread family (compile workers, page threads) — exactly the fragile choreography to avoid.

**Adopted: a watchdog that signals the worker by thread id.**

- The worker posts `{script, deadline}` into a small mutex/condvar-guarded slot before executing each script and clears it after (two mutex ops per script — noise against compile+execute cost).
- The watchdog — either the otherwise-idle main thread waiting on that condvar with a timeout, or a tiny dedicated monitor thread — fires `pthread_kill(worker_tid, SIGALRM)` on expiry. Delivery is **thread-directed**: it lands on the batch worker regardless of any other thread's mask. The handler still `siglongjmp`s to the per-script timeout recovery point, exactly as today; only the *delivery* mechanism changes.
- `alarm()` is removed rather than tamed. The design stays deterministic under any future thread population, and scales to page isolates as per-page watchdog entries — same mechanism, N slots.

**Successor (recorded, not v1 scope):** a cooperative interrupt flag checked at loop back-edges / park points — the browser "slow script" mechanism, and the very safepoints K30c already wants for task cancellation. When those safepoints exist, script timeout becomes `cancel`-shaped and the signal path remains only as the backstop for wedged *native* code in the test harness. The timeout roadmap therefore merges into the K30c safepoint work item instead of being its own track.

---

## 6. Further caveats (gaps in v1)

- **Caveat 3 — the whole protocol moves.** stdin parsing, inline-source blob reads, and `BATCH_START/END/EXIT` writes all execute on the worker; the shell writes nothing while a batch is live. No cross-thread handoff of protocol state exists — ordering stays single-threaded as today.
- **Caveat 4 — uv-loop affinity (K3/K10).** uv loops are not thread-safe. The loop is created, pumped, drained, and shut down **only on the executing thread** — the CLI helper already conforms (`js_event_loop_shutdown()` + `lambda_uv_cleanup()` on the helper thread, `main.cpp:184`); batch and Radiant keep this true as they move. Today that thread owns the process-global `g_loop` plus the static microtask/nextTick/rAF rings; under RC2 the loop becomes per-context (K3's "one loop per context") — same rule, N instances. Cross-thread wake-ups, when they arrive (RC4 event routing, K20 `send`), enter via `uv_async` — never by touching another thread's loop.
- **Caveat 5 — thread identity of initialization.** Everything context-scoped is created on the worker: the hot `EvalContext` (`js_test262_hot_context_create`), heap/nursery/name_pool, MIR contexts, `lambda_stack_init()`, `sigaltstack`. Rule of thumb: after `pthread_create`, the shell touches no runtime state.
- **Caveat 6 — exit paths.** `process.exit()` from the worker exits the process — intended Node semantics; `atexit` handlers run on the exiting thread. The batch's per-mode lifecycle suppression (`js_batch_execution_mode`) is orthogonal and unaffected.
- **Caveat 7 — between-test crashes and worker replacement.** v1 semantics kept: a between-test crash emits `BATCH_EXIT` and the harness retries scripts individually; the RSS-limit bailout (`main.cpp:4438`) continues to govern leak accumulation across recoveries. Recorded option (JO3): the shell replaces a wrecked worker with a fresh thread + fresh hot context and resumes the manifest — strictly better isolation and exactly the isolate-restart shape, but not needed for v1.

### 6.5 The global-state ledger — what gates *concurrent* isolates

Everything in this doc so far is safe under the **single-executor** rule (at most one thread executing JS at any moment), because the runtime religiously resets shared globals between runs. Recording what is already thread-clean vs. what must become per-context/TLS before RC2 runs two page threads *simultaneously* — this list is the entry criteria for Radiant sequencing step 2:

| State | Today | Concurrent-isolate requirement |
|---|---|---|
| `context` (EvalContext ptr), `input_context` | **`__thread`** (`runner.cpp:296`) — clean | none |
| lambda-stack recovery (`_lambda_recovery_point/_armed`, bounds) | **`__thread`** — clean | none |
| **`js_runtime_state`** — exception flag/value, `current_this`, `new_target`, `module_vars[]`, regexp last-match, arg scratch (`js_runtime_state.cpp:4`) | **plain process-global** — the biggest blocker | move into `EvalContext` or TLS; it is "the JS registers" and belongs to the context by construction |
| MIR globals — `g_active_mir_ctx`, `g_active_mir_transpiler`, preamble mode flags (`js_mir_internal.hpp:20–43`) | process-global "current compile/exec" pointers | per-context or TLS; used by crash/timeout cleanup, so JT4's per-thread recovery naturally wants them TLS |
| uv loop `g_loop` + timer table + microtask/nextTick/rAF rings (`lib/uv_loop.c:11`, `js_event_loop.cpp`) | process-global, single | loop-per-context (K3); rings move into the loop instance |
| Signal dispositions | process-global by OS design | stays global — JT4's chain dispatches on thread-local state |
| name-pool release hook `g_name_pool_node_release` | global fn ptr | per-context pool already; hook is set-once — audit only |

Batch and CLI never need this table fixed (one executor forever). It is future-RC2 work, but every JT decision above is chosen so that fixing the table is *additive* (make state per-context) rather than corrective (undo a topology).

---

## 7. Alignment map

| This doc | Concurrency ledgers |
|---|---|
| JT1 context-thread rule; shell never executes script | RC2 page isolate; §5.5 "context on its own thread"; §5.5 "main context owns Radiant/UI" |
| JT3 browser profile: small limit, `RangeError`, page survives | RC7 crash containment |
| JT4 handlers-once / state-per-thread / per-thread altstack | prerequisite for RC2 multi-page and RC7 reporting |
| JT5 watchdog now → safepoint interrupt flag later | K30c cancellation safepoints |
| JT6 loop affinity; cross-thread entry via `uv_async` only | K3/K10 one loop per context; RC4 mailbox delivery |
| Parallel module *compile* workers unchanged | RC6-shaped internal fork-join — invisible to execution semantics |

---

## 8. Sequencing

1. **Batch worker thread** — JT2 + JT4 thread-local conversion + JT5 watchdog, landed together (they touch the same code). Expected result (v1's, still the acceptance test): direct CLI, focused gtests, and `js-test-batch` share one stack budget; `tco.js` / `lib_ajv.js` stop being batch-only crashers.
2. **Recovery-kit consolidation (JT4b)** — fold the batch and Radiant guards into the chained process handler. Deliverable: deep recursion yields `RangeError: Maximum call stack size exceeded` at **every** entry point (today CLI-only), instead of a recovered exit-139 crash in batch and Radiant.
3. **View/render** — stays main-thread under the browser profile until Radiant sequencing step 2 (page isolates); JT4/JT5/JT6 plus the §6.5 global-state ledger are the entry criteria for that step, so nothing done here is thrown away.

## 9. Decision ledger (JT)

- **JT1** — Context-thread rule: every JS/Lambda context executes on a thread it owns; the process main thread is the shell and never executes script. (Generalizes v1's invariant; = RC2/§5.5 applied to today's CLI.)
- **JT2** — `js-test-batch` moves wholly onto **one** worker pthread (256 MB, whole loop incl. stdin protocol, hot-context reuse, per-script recovery); no thread-per-script. *(= v1 proposal, adopted)*
- **JT3** — Stack **budget ≠ limit**: native stack size is a per-profile engineering budget (node = 256 MB; browser = modest); the JS-visible limit is the lambda-stack guard's `RangeError` conversion — already live on the CLI path, extended to all paths by JT4b. Page JS never gets the 256 MB helper.
- **JT4** — Signals: handlers installed once per process; recovery state (`jmp_buf`s, armed flags) **`__thread`**; `sigaltstack` per executing thread with per-thread storage. **JT4b:** consolidate batch/Radiant/lambda-stack guards into one chained process handler (overflow check → embedding crash-catcher → default), ending the last-install-wins fight and making `RangeError` uniform.
- **JT5** — Timeouts: v1's mask-choreography option **rejected** (uv-threadpool mask inheritance makes process-directed SIGALRM non-deterministic); adopted: watchdog + `pthread_kill(worker, SIGALRM)`; successor merges into K30c safepoints.
- **JT6** — uv-loop affinity: each context's loop lives and dies on its context thread; cross-thread entry only via `uv_async`.
- **JT7** — layout/render/view interim: page JS on the main thread under the browser profile; end state is the RC2 page thread (macOS main-thread-UI requirement forces it regardless).

## 10. Open items (JO)

- **JO1** — Browser-profile limit value: measure MIR-frame depth on representative page scripts before fixing the number.
- **JO2** — `siglongjmp` hygiene: the recovery paths (overflow *and* crash-catcher) skip C++ destructors and MIR/transpiler cleanup — the batch already budgets ~55 MB leak per recovery (`main.cpp:4429`). Acceptable for a test harness; for Radiant pages surviving a `RangeError`, audit what state the skip can corrupt beyond leaks (the `js_batch_cleanup_unsafe` pattern in `script_runner.cpp` is the current mitigation).
- **JO3** — Batch worker replacement after between-test crashes (Caveat 7) — worth doing when it removes real harness retries, not before.
- **JO4** — REPL / `-i -e` path: confirm per-evaluation use of the execution-stack helper is acceptable (thread create per eval) or hoist to one persistent REPL context thread — the JT1-conformant shape.
- **JO5** — §6.5 global-state migration (esp. `js_runtime_state` → `EvalContext`): scope and sequence when RC2 page isolates are scheduled; not needed for any single-executor mode.

---

## 11. Implementation plan

Three phases matching §8. P1 and P2 are each one PR-sized unit landed green; P3 is a small follow-up. Every phase ends at the full gate set: `make test-lambda-baseline` + `make test-radiant-baseline` (100%), JS gtests, `make node-baseline` (≥ current 1492/3517), test262 batch, editor JS suite (1931).

### P1 — batch worker thread (JT2 + JT4 thread-locals + JT5 watchdog)

Landed as one change — the three pieces touch the same ~500 lines of `main.cpp` and are individually untestable (a worker thread with `alarm()` is broken; a watchdog without the worker is pointless).

**P1.1 — generalize the thread-spawn helper.** Turn `js_cli_transpile_with_execution_stack` (`main.cpp:189–223`) into a reusable
`int run_on_js_execution_thread(void* (*entry)(void*), void* arg, size_t stack_size)`
keeping its exact behavior: `pthread_attr_setstacksize`, create, join, and **fallback to inline execution on any pthread failure** (`main.cpp:202/207/215`). The CLI path becomes its first client (no behavior change); the batch worker its second. Windows: implement via `_beginthreadex` with the `stack_size` argument (CreateThread takes stack size directly), so Windows batch gains the 256 MB budget too even though the signal guards remain POSIX-only.

**P1.2 — extract the batch loop into the worker entry.** Move `main.cpp:4092–4556` wholesale into `static void* js_batch_worker_main(void*)` with an args struct `{batch_timeout, hot_reload, diagnose, exit_code}`:

- Moves to the worker (Caveat 5 — created on the thread that uses them): `runtime_init` (`:4092`), `lambda_stack_init()` (`:4094`), the `sigaltstack` block (`:4096–4108` — see P1.4), hot-context creation (`:4113–4122`), preamble state, the entire `while (fgets(...))` protocol loop including all `BATCH_*` writes, and the tail cleanup.
- Stays on main (pre-spawn): arg parsing, `log_disable_all()`, `js_batch_execution_mode` default. Post-spawn, main runs only the watchdog loop (P1.5) and then `lambda_main_finish(args.exit_code)` after join.
- `BATCH_EXIT` paths become `return` from the worker with the exit code in the args struct.

**P1.3 — thread-local recovery state.** `batch_crash_jmp`, `batch_crash_active`, `batch_timeout_jmp`, `batch_timeout_active`, `mir_error_jmp`, `mir_error_active` (`main.cpp:1308–1350`) become `static __thread`. The handler *installation* (`main.cpp:4135–4148`) stays once-per-process (it can remain in the worker's prologue — first and only executor installs). `g_batch_mir_error_handler` (the MIR hook) stays a process global; only the jmp state it reaches through goes TLS. Do Radiant's `js_exec_jmpbuf` / `js_exec_guarded` / `js_exec_timed_out` (`script_runner.cpp:65–68`) in the same sweep — free now, required by P2.

**P1.4 — per-thread altstack, deduplicated.** `lambda_stack_init()` already installs a per-thread `sigaltstack` (`lambda-stack.cpp:209`, 64 KB); the batch's manual `static char alt_stack_mem[131072]` (`main.cpp:4102–4107`) becomes redundant once the worker calls `lambda_stack_init()` first. Delete the manual block; raise `LAMBDA_ALT_STACK_SIZE` to 128 KB to keep the batch's more generous headroom (its handler path does more work). Verify with a forced-overflow script under the batch that the handler runs on the altstack (no double fault).

**P1.5 — watchdog timeout.** Small static struct in `main.cpp` (or `lib/` if the Lambda `test-batch` adopts it too, P1.7):

```c
struct JsWatchdog {
    pthread_mutex_t mu; pthread_cond_t cv;
    pthread_t target;            // batch worker tid
    bool armed; bool worker_done;
    struct timespec deadline;
};
```

- Worker: `watchdog_arm(timeout_s)` replaces `alarm(batch_timeout)` (`main.cpp:4346`); `watchdog_disarm()` replaces every `alarm(0)` (`:4332, 4352, 4366`). Both are mutex-guarded flag flips + `cv` signal — two lock ops per script, noise next to compile+execute.
- Main thread (already idle): loop on `pthread_cond_timedwait` — wait until `deadline` when armed, forever when not; on expiry with `armed` still set, `pthread_kill(target, SIGALRM)` (one shot per arm). Exit the loop when `worker_done`.
- Ordering invariant: the worker sets `worker_done` under the mutex **before** returning, so the watchdog can never signal a dead thread id.
- `SIGALRM` disposition (`batch_alarm_handler`) is installed **once** in the worker prologue next to the crash handlers — deleting the per-script `sigaction` install/restore dance (`main.cpp:4337–4341, 4370`). The handler body is unchanged: `siglongjmp(batch_timeout_jmp, 1)` — now through TLS state; delivery lands on the worker by construction (thread-directed), so no masks are set anywhere.
- Delete both uses of `alarm()`; grep-gate: no `alarm(` remains in the js-test-batch path.

**P1.6 — acceptance.**
- `printf 'test/js/tco.js\ntest/js/lib_ajv.js\n' | ./lambda.exe js-test-batch` → both pass (the v1 acceptance test).
- A `while(true){}` script through the batch → result 124 via the watchdog; a crashing script → 128+sig recovery unchanged; a between-test crash → `BATCH_EXIT` unchanged.
- Full JS gtest suite + test262 batch: pass counts move only upward (tco/ajv-class scripts flip); batch wall-time within noise of before (hot reload preserved).
- Remove tco.js / lib_ajv.js from any crash-expected or skip lists in `test_js_gtest.cpp` / baselines.

**P1.7 (optional, same mechanics)** — the Lambda `test-batch` handler (`main.cpp:3942–4054`) still uses `alarm()` on the main thread; port it onto the same helper + watchdog once P1 proves out. Separate commit; `make test-lambda-baseline` is its gate.

### P2 — recovery-kit consolidation (JT4b)

**P2.1 — one chained handler.** New `lambda/lambda-recovery.{h,cpp}` (or grow `lambda-stack.cpp` — decide by size; the chain logic is ~100 lines):

- `lambda_recovery_install_process(void)` — `pthread_once`-guarded install of the single SIGSEGV/SIGBUS/SIGABRT/SIGTRAP handler (+ SEH shim on Windows, existing `lambda-stack` pattern).
- Per-thread embedding tier: `lambda_recovery_arm(sigjmp_buf* jb)` / `lambda_recovery_disarm()` — a `__thread` pointer (a one-deep slot is sufficient today; make it a small stack only if the event-loop drain guard in P2.2 needs nesting).
- Handler chain, in order: (1) `is_stack_overflow_fault(si_addr)` && `_lambda_recovery_armed` → lambda-stack recovery → `RangeError` (most precise check first); (2) thread's embedding `jmp_buf` armed → `siglongjmp(jb, sig)`; (3) restore `SIG_DFL`, re-raise. SIGALRM stays a separate, simpler disposition (timeout tier, P1.5).

**P2.2 — convert the three users.** Batch (`batch_crash_handler` + install block) and Radiant (`js_exec_crash_handler`, `script_runner.cpp:90–112`, plus its `sigaction` save/restore pairs at `:2032–2087`) delete their handlers and arm/disarm the shared kit around script execution. Audit the event-loop drain's nested SIGSEGV guard (`js_event_loop.cpp:1837–1898`) — convert it to the embedding tier or leave with a comment stating why it self-manages; do not convert blind. `mir_error_jmp` is a `longjmp` hook, not a signal — untouched beyond P1.3's TLS move.

**P2.3 — acceptance: `RangeError` everywhere.** New regression script `test/js/stack_overflow_range.js` — runaway recursion in `try/catch`, asserts `e instanceof RangeError` and that code after the catch runs. Gate: passes identically via (a) `lambda.exe js`, (b) `js-test-batch`, (c) focused gtest, (d) a Radiant smoke page (`layout` with an inline overflow script — page survives, layout completes). Bookkeeping fallout to expect: batch overflow scripts stop counting as crashes (`128+11` → clean result), so any harness expectations encoding exit 139 or crash-counts move — update `test_js_gtest.cpp` / baseline files in the same change.

### P3 — stack profiles (JT3)

Name the constants and thread them through: `JS_STACK_NODE_PROFILE` (256 MB — CLI + batch, exists) and `JS_STACK_BROWSER_PROFILE` (page threads when RC2 lands; until then Radiant keeps the main-thread stack). `run_on_js_execution_thread` takes the profile. JO1 (measuring MIR frame depth to pick the browser limit) blocks only the browser number, nothing else. No behavior change in this phase — it's naming so RC2's page-thread work starts from constants, not literals.

### Explicitly not in this plan

- View/render threading (waits for Radiant sequencing step 2 — page isolates).
- §6.5 global-state migration (JO5; gates RC2, not any single-executor mode).
- Safepoint-flag timeouts (successor rides K30c's cancellation safepoints).
- JO3 worker replacement, JO4 REPL context thread — tracked, demand-driven.

### Touchpoint summary

| File | P1 | P2 |
|---|---|---|
| `lambda/main.cpp` | extract worker, TLS statics, watchdog, delete `alarm()`/manual altstack | swap batch guard onto shared kit |
| `lambda/lambda-stack.{h,cpp}` | altstack size 128 KB | host or expose the chained handler |
| `lambda/lambda-recovery.{h,cpp}` (new) | — | chain + arm/disarm API |
| `radiant/script_runner.cpp` | TLS statics | swap `js_exec` guard onto shared kit |
| `lambda/js/js_event_loop.cpp` | — | audit drain guard |
| `test/test_js_gtest.cpp`, baselines | un-skip tco/ajv | exit-139 → RangeError expectations |
| `test/js/stack_overflow_range.js` (new) | — | the P2 gate |

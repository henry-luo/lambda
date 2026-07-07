# Lambda Concurrency Design — Colorless Async, Isolates, and the Four-Level Model

**Status:** approved direction — design doc for discussion & refinement
**Date:** 2026-07-06
**Context:** realizes decision **J4** (BEAM-style concurrency, stated explicitly) from the Jube runtime ledger — Part 1 of `Lambda_Semantics_Features.md`. Addresses the #1 structural gap from the six-language feature comparison (Part 3 of the same doc). Decisions confirmed by the user in the 2026-07-06 foundation discussion are marked in the ledger (§8) as K1–K10; open points as O1–O10.

---

## 1. The model at a glance

Lambda concurrency is layered in four levels. Levels 1–3 are the core design; level 0 is a free win enabled by `fn` purity and included for completeness.

| Level | Name | Unit | Isolation | Communication | Parallel? | Nearest precedent |
|---|---|---|---|---|---|---|
| **0** | Data parallelism | none (implicit) | n/a — pure `fn` only | n/a | yes | pure parallel map |
| **1** | Colorless async | fiber (task) | shared context | awaitables | no (concurrent, single-threaded) | Java Loom / Go netpoller |
| **2** | Worker thread | isolate | own heap, own context | messages + read-only shared flats | yes | JS Workers / Ruby Ractors |
| **3** | Child process | isolate | OS process | messages + read-only shared flats | yes | BEAM |

**Governing principles:**

1. **`fn` never suspends, never observes concurrency.** Async, tasks, messages — all of it is effectful, therefore `pn`-territory. The purity color is the *only* color in the language (K1).
2. **Colorless among `pn`s.** A `pn` that awaits is indistinguishable, to its callers, from one that doesn't. No `async` keyword, no signature change, no third color. Async coloring collapses into the purity coloring Lambda already has (K2).
3. **Nondeterminism stays confined to `pn`.** Task interleaving, message arrival order, isolate scheduling — all only observable from `pn`. The C1–C12 semantic core survives concurrency untouched.
4. **Levels 2 and 3 are one API, not two features.** Same primitives (`spawn`/`send`/`receive`), same semantics; the isolation level is a spawn option. Code written against workers moves to processes without rewriting (K4).
5. **No shared mutable state, at any level.** Sharing is read-only and restricted to flat values (§5.2). This is what makes the whole design safe on C4 value semantics — the BEAM/Clojure lesson.

---

## 2. Prior art

### 2.1 Go goroutines — the full machinery, and which parts Lambda takes

A goroutine is a **stackful coroutine, scheduled in user space, M:N-multiplexed over OS threads**. Four mechanisms:

1. **Small, growable, contiguous stacks.** Each goroutine starts at ~2–8 KB heap-allocated. Every function prologue checks for overflow ("morestack"); on overflow the runtime allocates a stack 2× larger, **copies the old stack, and rewrites every pointer into it**. Stack relocation is only possible because the Go compiler emits **precise stack maps** (exact location and type of every pointer in every frame). Go's first design — segmented stacks — was abandoned for the "hot split" performance cliff; contiguous-with-copying replaced it in Go 1.4.
2. **Cheap context switch.** Park = save SP, PC, and a few callee-saved registers into the `g` struct (`gobuf`); resume = restore. Tens of nanoseconds. Hand-rolled assembly (~30 lines/arch) — deliberately *not* `ucontext`/`swapcontext`, which burns a syscall on signal-mask save/restore.
3. **G–M–P scheduler.** G = goroutine, M = OS thread, P = processor (run-queue token, `GOMAXPROCS` of them). Local run queues with work stealing. Preemption was cooperative (prologue checks) until Go 1.14 added async preemption via `SIGURG`.
4. **The netpoller — where "colorless" actually happens.** `conn.Read()` *looks* blocking, but: the fd is registered with epoll/kqueue, the goroutine is **parked**, the M runs another G, and on readiness the goroutine resumes as if the call had blocked. Synchronous surface, asynchronous machinery, no `async` keyword. Blocking cases Go can't park — raw syscalls, cgo frames — **detach the OS thread** (P handed to another M) so other goroutines keep running.

**Lambda takes:** the hand-rolled context switch (2) and the netpoller pattern (4). **Lambda skips:** growable/copyable stacks (1) — they require precise-map infrastructure Lambda doesn't have yet (G1) and pointer rewriting Lambda never needs to buy; and G-M-P work stealing (3) — fiber migration across threads is the hardest part of Go's runtime, and Lambda's isolate topology (§5.5) makes it unnecessary. The thread-detach treatment of unparkable frames returns as the native-module rule (§4.4).

### 2.2 Java Loom — proof that colorless retrofits onto a JIT'd runtime

Virtual threads (JDK 21, 2023) deliver exactly "colorless async" on a 30-year-old runtime: park copies the continuation's stack segment to the heap; resume copies it back onto a carrier thread. Millions of virtual threads; unchanged synchronous APIs; the JIT'd frames never know. Loom's one wart is precisely instructive: **pinning** — a virtual thread that parks while holding a native frame (JNI) or (pre-24) a `synchronized` monitor pins its carrier thread. Jube's native modules hit the same wall; §4.4 is the clause that handles it. Loom is the existence proof for K2; its pinning problem is the reason §4.4 is non-negotiable.

### 2.3 Zig — the cautionary tale: don't do colorless halfway

Zig attempted colorless async via compile-time dual compilation: every potentially-async function compiled in both sync and async (CPS-transformed) variants, with the compiler propagating "async-ness" through the call graph. The complexity cascaded — indirect calls, function pointers, the C ABI boundary, doubled compile artifacts — and the feature was **removed from the language** (gone since 0.11, still absent). The lesson is not "colorless is wrong" (Loom proves it's right); it's that **compiler-transform colorlessness is coloring with extra steps**, and it collapses at exactly the places Lambda has in abundance: closures, HOFs, JIT'd code, and a C ABI. Stackful or nothing — hence K2's fiber commitment.

### 2.4 BEAM — the semantic model for levels 2/3

Per-process heaps and stacks; messages **copied** between processes — with one exception: **large binaries are refcounted and shared**, because they're flat and immutable. Cooperative scheduling by reduction counting. Links, monitors, and supervision trees turn isolation into fault tolerance ("let it crash"). Distribution uses one wire format (ETF).

**Lambda takes:** copy-by-default messaging with flat-immutable sharing as the exception (§5.2 — BEAM's binaries generalize to Lambda's typed arrays/strings/buffers); exit notifications as the failure surface (§5.3); Mark as the wire format (Lambda already has what BEAM had to invent). **Lambda learns from BEAM's scar:** unbounded mailboxes are a famous production footgun (memory blowup under slow consumers) — Lambda defaults bounded (O3).

### 2.5 JS Workers / Node — the level-2 surface and the interop constraint

Web Workers are the proven *surface* for isolate-style threading (isolated heap, `postMessage`, structured clone ≈ Item copy). Node's mistake is the one K4 avoids: `Worker` and `child_process` are unrelated APIs for the same semantic idea. The binding constraint JS imposes on level 1: **run-to-completion** — a JS frame may only yield at its own `await`/job boundaries, never because Lambda's scheduler decided to. This forces (and justifies) the one-loop-per-context design in §4.2. Ruby's Ractors independently confirm the sharing rule: only deep-frozen (immutable) objects are shareable across Ractors.

---

## 3. Level 0 — deterministic data parallelism

A `par`-variant of pipe/map that applies a **pure `fn`** to collection elements in parallel:

```
let thumbs = images |par> ~ |> resize(~, 128, 128)     // syntax TBD (O7)
```

- Purity guarantees the result is **identical to the sequential run** — scheduling is invisible, no semantic novelty reaches user code. This is concurrency with zero new semantics, and only Lambda's `fn`/`pn` split makes it statically safe (Python/JS cannot offer this guarantee).
- The lambda must be `fn`-pure; passing a `pn` is a **compile-time error**.
- Implementation: internal worker pool over the level-2 substrate; chunked work distribution; no user-visible tasks, handles, or messages.
- This is the `par-each` idea from the Nushell comparison (features doc §2.5), landed in the level model as the cheapest win.

---

## 4. Level 1 — colorless async

### 4.1 Language surface

- **`await expr` is a `pn`-only operation.** Using it in an `fn` is a compile error (K1). No `async` marker exists anywhere in the grammar.
- A `pn` that awaits is **invisible to its callers** — same type, same call syntax, same ABI. Colorless among `pn`s (K2).
- Awaitable things: async host operations (`io.*`, timers, net), spawned isolates' results and `receive` (levels 2/3), JS Promises (§4.5), and level-1 task-scope children (O1).
- Error integration: awaiting a failed operation yields the error as a value — `await` composes with `T^E` and `^`-propagation exactly like any other `pn` call. No new error channel (extends J3).
- Structured scopes (task groups with scope-bound children, cancellation on scope exit — the Trio/Kotlin/Swift consensus) are the intended surface for spawning *within* level 1; syntax and semantics are open (O1, O2). Note the design adjacency to the `defer`/`with` resource-cleanup gap (features doc §3.1) — same construct family, design together.

### 4.2 Runtime design (confirmed — K2)

The implementation is the cheap slice of Go/Loom: **fixed mmap'd fiber stacks, a ~30-line context switch, one event loop per context, park/resume at `pn` await points.**

- **Fiber stacks:** reserve ~1 MB of virtual address space per fiber via `mmap`, lazily committed, with a guard page for overflow detection. A parked fiber typically holds a few KB of committed pages. **No growth, no copying, no pointer rewriting** — this deliberately sidesteps the precise-stack-map machinery Go's relocation requires (and which Lambda lacks until G1 lands). The cost — thousands of fibers, not millions — matches the domain: documents, pipelines, a Radiant desktop app, never 1M concurrent connections.
- **Context switch:** hand-rolled save/restore of callee-saved registers + SP, per architecture (x86-64 SysV, AArch64). Explicitly **not** `ucontext`/`swapcontext` (sigmask syscall per switch). This is the libco/Boost.Context design.
- **One event loop per context, and it is the SAME loop as the JS event loop.** One reactor (kqueue/epoll; IOCP on Windows) per context; JS jobs, timers, and fiber resumes interleave on it. `await` = register continuation with the loop + park the fiber; the scheduler runs the next runnable fiber or polls. This is Go's netpoller pattern, single-threaded per context. JS run-to-completion is preserved: JS frames yield only at their own await/job boundaries (K3).
- **The MIR JIT needs zero changes.** JIT'd frames are ordinary native frames on the fiber's stack; the JIT never knows a switch happened. No compiler transform, no dual compilation (the anti-Zig property). This is the decisive argument for stackful over CPS.
- **Cooperative scheduling only (v1).** Suspension happens at explicit points: `await`, `receive`, channel/mailbox ops. No preemption — Lambda scripts aren't adversarial, and BEAM ran on cooperative reduction-counting for decades. Loop-back-edge safepoints can be added later if runaway-`pn` starvation becomes real.
- **`fn` call trees are suspension-free regions.** Since only `pn` chains can park, the runtime needs no safepoints, no reentrancy caution, and no rooting discipline inside pure code — the purity color does implementation work here too.

### 4.3 GC interaction — the G1 dependency

- Parked fiber stacks are GC roots. **v1: conservatively scan the committed portion** of each parked stack (fixed, non-moving stacks make this sound; Go itself shipped conservative scanning before 1.4). **Precise scanning arrives with G1.**
- Dependency direction, stated bluntly: **level 1 cannot ship before G1 is resolved** (`Lambda_GC_Root_Issue.md` — the blanket `MIR_T_I64` rooting that is simultaneously load-bearing and wrong). More live stacks make the blanket-rooting problem worse, not merely concurrent. Fibers *raise* G1's priority.

### 4.4 Native frames — the module-ABI clause (unretrofittable; write into JubeHostAPI v1)

- A fiber **must not park while a `JubeHostAPI` frame is on its stack**, unless the module declares that entry point await-safe. (This is Loom's pinning problem and Go's cgo case, met head-on.)
- A host call that genuinely blocks (sync file I/O in a module, a C library call) **detaches the OS thread** Go-style: the context's loop continues on another thread, the blocking call completes on the detached one. v1 may simplify to "blocking host calls pin the context" with a documented list, but the ABI *clause* — modules must declare blocking/await-safety — goes in v1, because it cannot be retrofitted once modules exist.

### 4.5 JS bridge

- Lambda `pn` awaiting a **JS Promise**: park the fiber; resume on settlement. Rejection surfaces as an **error value** (`T^E`) — no exception crosses the boundary (J3).
- Lambda async operations surface to JS as ordinary **Promises**; a JS `await` of a Lambda operation is just a promise await.
- One loop (K3) makes the bridge mechanical: promise settlement and fiber resume are entries on the same queue. No cross-loop marshalling exists because there is no second loop.

### 4.6 JS coexistence — two continuation mechanisms, one reactor (K10)

The question "should JS async migrate to fibers, or keep libuv?" dissolves into three layers with different answers. Grounded in the current code: LambdaJS's loop is already **libuv** (`js_event_loop.h` — drain via `uv_run` + microtask flush), and JS `async/await` is already lowered to **state machines** in the MIR compiler (`js_mir_function_class_lowering.cpp`, Phase 6 — resume slots sized by yield+await count).

**Layer 1 — the reactor: unify; libuv *is* the K3 loop.** libuv is the thing that waits (kqueue/epoll/IOCP); fibers and state machines are two *continuation mechanisms* hanging off it. Lambda `await` = start the uv operation, park the fiber; the uv callback resumes it — Go's netpoller with libuv playing netpoller. Two reactors in one thread is a broken design (who blocks on poll?); libuv already solved the portability matrix. Payoff: `js_fs`/`js_net`/`js_http`/timers become **shared uv-backed services with two thin fronts** — JS gets callbacks/promises, Lambda gets park/resume — instead of a parallel Lambda I/O stack for O8. Dovetails with the `node-*` native-module carve-out (module-design POC 2): the uv-backed services are those modules.

**Layer 2 — JS async semantics: do NOT migrate.** JS stays colored, on its existing state machines:
1. **JS coloring is spec, not implementation.** Microtask timing is observable, tested behavior (`await` yields to the job queue even for resolved values; `Promise.resolve().then` ordering; run-to-completion per job). Re-implementing JS `await` as fiber parking would still have to reproduce job ordering exactly — no semantic simplification exists, only risk.
2. **The mechanism is built and green** (editor JS suite, node baseline). Rewriting a working spec-compliant lowering for internal uniformity is negative-value work.
3. **Cost profile:** JS allocates promises in huge numbers; a state machine is a small heap object, a fiber is a mapped stack. Fiber-per-async-call is strictly heavier for the language that awaits the most.

Asymmetry is not incoherence: each language gets the mechanism its semantics demand, on one shared reactor.

**Layer 3 — the membrane rule.** What happens when JS synchronously calls a Lambda `pn` that awaits? The `pn`'s frames sit above JS frames on the same fiber stack; parking would suspend a JS job mid-execution while other jobs run — other JS code would observe state mid-job, violating run-to-completion. Mechanically possible, semantically forbidden. The rule:

> **A may-await `pn` invoked from JS is dispatched onto a fresh fiber and returns a Promise to JS immediately.** Colorlessness is a Lambda-internal property; at the JS membrane, awaiting `pn`s *appear* colored (promise-returning). The boundary adapter does the coloring — never the Lambda source.

Two supporting obligations (open items):
- **May-await analysis (O9):** the compiler must know, transitively, which `pn`s can await — a static effect bit in the same analysis family as the existing purity analysis. Invisible in Lambda source; consumed only by JS-facing wrapper generation (and usable by the runtime as a park-legality assertion).
- **Resume ordering (O10):** when a settled promise resumes a Lambda fiber, the resume is enqueued **after the current job's microtask drain** (macrotask-position), so JS job-queue invariants hold. The precise interleaving of fiber resumes vs. uv callbacks vs. microtask flushes must be specified once, in the loop.

---

## 5. Levels 2 & 3 — the unified isolate API

### 5.1 One primitive set, isolation as an option (K4)

```
let w = spawn("worker.ls", {isolation: 'thread'})     // level 2
let p = spawn("worker.ls", {isolation: 'process'})    // level 3 — same API
send(w, {cmd: 'resize', img: buf})
let result = await receive(w)        // or: await w — isolate's final result, T^E-shaped
```

(Syntax is a sketch — grammar is O7. The invariant is not the syntax; it is that **`spawn`/`send`/`receive` have identical semantics at both levels** and migration between levels is a one-option change.)

- An isolate = its own Lambda context: own heap, own fiber scheduler, own event loop (a level-2/3 isolate is "a context on its own thread/process" — the level-1 substrate, replicated).
- `spawn` returns a handle. Handles are awaitable (final result), receivable (message stream), and monitorable (§5.3).
- Level 2 = OS thread in the same address space. Level 3 = child process. Nothing else differs semantically.

### 5.2 Memory model

- **Messages are deep-copied by default.** Items cross by value — the C4-honest semantics. Same-address-space (level 2) may optimize to COW behind the same semantics; that is an implementation detail, never observable.
- **Read-only sharing is restricted to flat, pointer-free values**: typed arrays (`ArrayNum`), strings, binaries, image buffers. This is BEAM's refcounted-binaries move, generalized. Level 2 shares by pointer + refcount; level 3 maps the buffer via shm/mmap. A shared flat is deeply immutable — the Ractor rule.
- **General graph-shaped Items are never shared.** A position-independent frozen-arena format (offset-based Items) would be required and is **deferred indefinitely** — BEAM refused this for 25 years and never regretted it. Copy is the answer; flats are the escape hatch for the data that actually gets big (images, matrices, corpora).
- **Level-3 wire format = Mark.** Messages across the process boundary serialize as Mark — Lambda already owns what BEAM had to invent (ETF). This also makes level-3 messages naturally debuggable/loggable.
- Note under J5: JS `SharedArrayBuffer` + `Atomics` (shared *mutable* memory) are **excluded by design** — recorded as a deliberate dialect gap, not an omission.

### 5.3 Failure model — failures are values (extends J3)

- A crashed/exited isolate delivers an **exit notification**: awaiting its handle yields the result or the error as `T^E`; subscribers can receive an exit message (monitor-lite).
- No exception ever crosses an isolate boundary — a dead child is an error *value* in the parent, composing with `^`-propagation like everything else. This is J3 applied one level up, and it is what makes let-it-crash expressible.
- Full BEAM-style links and supervision trees are a **future layer** on top of exit notifications (O5). v1 ships monitors (exit messages); supervisors are userland until proven otherwise.

### 5.4 Mailboxes, `select`, backpressure

- **Bounded mailboxes by default** (BEAM's unbounded mailboxes are its best-known production footgun). The `send` policy when full — block, error-value, or drop-with-notification — is an open decision (O3); leaning: block with optional timeout, since blocking a fiber is cheap under level 1.
- **`select` is a required primitive**: wait on the first of several sources — multiple handles/receive arms, a timeout arm, a JS Promise. This is the one piece of Go's surface that survives (K8); BEAM's `receive ... after` and JS `Promise.race` are the same necessity. Syntax open (O4).

### 5.5 Scheduling topology — no fiber migration, ever (K7)

- Each isolate owns exactly one OS thread (level 2) or process (level 3), running its own fiber scheduler + event loop. **Fibers never migrate between threads.** Parallelism comes from having many isolates, not from work-stealing within one.
- This discards the hardest part of Go's runtime (cross-thread migration: TLS, stack aliasing, cross-thread scanning) while keeping its throughput shape: N isolates × M fibers each.
- It is also *required*, not merely convenient: JS run-to-completion demands single-threaded contexts (K3).
- **The main context owns Radiant/UI.** The Electron-replacement story (J5) is: main context runs Radiant + UI logic; workers run compute or a guest-language backend (e.g., Python via Jube); the UI thread never blocks because level 1 makes all its I/O awaitable.

---

## 6. Deliberately excluded

| Excluded | Why | What covers the need |
|---|---|---|
| Go-style unstructured `go` spawn | fire-and-forget is a liability; the industry spent a decade adding structure back (errgroup/context) | structured task scopes (O1) + `spawn` isolates |
| Channels as first-class values, unbuffered rendezvous | mailbox + select covers the actual use cases | `send`/`receive`/`select` |
| Fiber migration / work stealing | hardest part of Go's runtime; JS run-to-completion forbids it anyway | isolate topology (§5.5) |
| Preemptive scheduling (v1) | scripts aren't adversarial; BEAM-style cooperative sufficed for decades | cooperative parks; loop safepoints later if needed |
| Growable/relocatable stacks | requires precise-map + pointer-rewrite machinery (the hard 40% of Go) for a scale (1M fibers) the domain doesn't need | fixed mmap'd stacks, lazy commit |
| Shared mutable memory (incl. JS `SharedArrayBuffer`/`Atomics`) | the entire design rests on immutable sharing; C4 | flat read-only sharing + messages |
| Whole-program CPS / compiler-transform async | the Zig lesson: coloring with extra steps; breaks at closures, HOFs, JIT, C ABI | stackful fibers |

---

## 7. Guest languages on the concurrency substrate

- **JS/TS:** Promises/`async-await` run unchanged — colored, on their existing state machines, over the shared libuv loop (K10, §4.6). Calls into may-await `pn`s auto-promote to Promise at the membrane (§4.6). `Worker` maps to `spawn {isolation:'thread'}` with structured clone = Item copy. `SharedArrayBuffer` excluded (§5.2).
- **Python/Ruby/Bash (Jube):** guest threading APIs (`threading`, `Thread`, subshells) map — under the J5 dialect banner — onto isolates or are documented-unsupported. `asyncio`/Fiber interop is a per-guest mapping table, same family as the G3 error-mapping tables. No GIL emulation: guests get real isolates or nothing.
- Native modules: §4.4 clause (declare blocking/await-safety) + G2 (rooting across the ABI) are the two JubeHostAPI v1 obligations this design adds.

---

## 8. Decision ledger

**Confirmed (K):**

- **K1** — `fn` never suspends, never observes concurrency; `await` is `pn`-only. No third color: async coloring collapses into the existing purity color. *(user-confirmed)*
- **K2** — Colorless-among-`pn`s is implemented by **stackful fibers**, not compiler transform: fixed mmap'd lazily-committed fiber stacks with guard pages, hand-rolled ~30-line context switch (no `ucontext`), park/resume at `pn` await points. MIR JIT unchanged. *(user-confirmed)*
- **K3** — One event loop per context, **shared with the JS event loop**; JS run-to-completion preserved; promise settlement and fiber resume are the same queue.
- **K4** — Levels 2 and 3 are **one API** (`spawn`/`send`/`receive` + handles); isolation level is a spawn option; migration is a one-option change. *(user-confirmed)*
- **K5** — Copy messages by default; share **only flat pointer-free immutable values** (typed arrays, strings, binaries); frozen-graph sharing deferred indefinitely; **Mark is the level-3 wire format**.
- **K6** — Failures are values across every boundary: awaited handles yield `T^E`; exit notifications, no cross-isolate exceptions (extends J3).
- **K7** — No fiber migration; one thread per isolate; parallelism only via isolates (also required by K3).
- **K8** — Go coroutines excluded as a surface feature (no unstructured `go`, no channel values); **`select` is retained** as a required primitive. *(user-confirmed)*
- **K9** — Level 0 (deterministic `par` map over pure `fn`) is part of the model — the cheapest win, uniquely enabled by the `fn`/`pn` split.
- **K10** — **libuv is the shared reactor** implementing the K3 loop; **JS async stays colored on its existing state machines** (spec-governed, built, green); Lambda fibers and JS jobs are two continuation mechanisms on one loop; uv-backed I/O services are shared with two thin fronts. At the membrane, a may-await `pn` called from JS auto-promotes to a fresh fiber + Promise (§4.6). *(user-confirmed)*

**Open (O):**

- **O1** — Structured task-scope construct for level-1 spawning (syntax + semantics; children can't outlive scope; design jointly with `defer`/`with` cleanup).
- **O2** — Cancellation semantics: delivery at park points; what a cancelled `await` returns (`T^E` error code?); scope-exit cancellation ordering.
- **O3** — Mailbox bound + full-mailbox `send` policy (leaning: block with optional timeout).
- **O4** — `select` syntax (receive arms + timeout arm + awaitables).
- **O5** — Supervision layer scope: v1 = monitors/exit messages only; links/supervisors userland or built-in later.
- **O6** — Typing of unawaited results: is a handle a `task(T^E)`-like value? How do handles interact with C4 value semantics (they are identities, like the deferred ref cells — same family as Clojure atoms)?
- **O7** — Grammar: `await`, `spawn`, `send`/`receive`, `select`, `|par>` — all surface syntax TBD; must not collide with C9a quote-splice or existing sigils.
- **O8** — Async host-API surface: which `io.*`/net/timer operations become awaitable in v1 (implemented over the shared uv services, K10).
- **O9** — **May-await analysis**: transitive static effect bit on `pn`s (same family as purity analysis); consumed by JS-facing wrapper generation (§4.6) and as a runtime park-legality assertion. Must handle indirect calls through closures/HOFs conservatively.
- **O10** — **Loop ordering spec**: fiber resumes enqueue at macrotask position (after the current job's microtask drain); one written spec for the interleaving of uv callbacks, microtask flushes, and fiber resumes.

---

## 9. Dependencies & sequencing

1. **G1 (GC rooting precision) gates level 1.** Conservative scanning of parked fiber stacks is the sanctioned interim, but the blanket-rooting issue (`Lambda_GC_Root_Issue.md`) worsens with more live stacks — fix G1 first or in lockstep.
2. **JubeHostAPI v1 must include** the §4.4 native-frame clause (blocking/await-safety declaration) and the G2 rooting-across-ABI clause **before modules proliferate** — both are unretrofittable.
3. Suggested build order: **level 0** (small, isolated, immediately useful for the typed-array/image workloads) → **fiber substrate + level 1** (the big one; unblocks async I/O and the Radiant UI story) → **level 2** (isolate = context-on-a-thread; level 0 then migrates onto it) → **level 3** (same API over processes + Mark wire).
4. The per-guest concurrency mapping tables (§7) join the G3 error-mapping tables as module/ABI-spec work items.

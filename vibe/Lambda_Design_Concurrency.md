# Lambda Concurrency Design — Colorless Async, Isolates, and the Four-Level Model

**Status:** approved direction — design doc for discussion & refinement
**Date:** 2026-07-06; **revised 2026-07-08** — level-1 implementation redesigned (**K2-R**, §4.2): declared-`async` leaves + may-await state-machine compilation replaces stackful fibers as the adopted mechanism; the fiber design is retained in §4.2.4 as the reference alternative
**Context:** realizes decision **J4** (BEAM-style concurrency, stated explicitly) from the Jube runtime ledger — Part 1 of `Lambda_Semantics_Features.md`. Addresses the #1 structural gap from the six-language feature comparison (Part 3 of the same doc). Decisions confirmed by the user in the 2026-07-06 foundation discussion are marked in the ledger (§8) as K1–K10; open points as O1–O10.

---

## 1. The model at a glance

Lambda concurrency is layered in four levels. Levels 1–3 are the core design; level 0 is a free win enabled by `fn` purity and included for completeness.

| Level | Name | Unit | Isolation | Communication | Parallel? | Nearest precedent |
|---|---|---|---|---|---|---|
| **0** | Data parallelism | none (implicit) | n/a — pure `fn` only | n/a | yes | pure parallel map |
| **1** | Colorless async | task (parked state machine; fibers = reference alt.) | shared context | awaitables | no (concurrent, single-threaded) | Kotlin mechanics / Koka inference / Go netpoller surface |
| **2** | Worker thread | isolate | own heap, own context | messages + read-only shared flats | yes | JS Workers / Ruby Ractors |
| **3** | Child process | isolate | OS process | messages + read-only shared flats | yes | BEAM |

**Governing principles:**

1. **`fn` never suspends, never observes concurrency.** Async, tasks, messages — all of it is effectful, therefore `pn`-territory. The purity color is the *only* color in the language (K1).
2. **Colorless among callers.** `async pn` declares a suspension *source* at the leaf (K2-R); but a `pn` that merely awaits is indistinguishable, to its callers, from one that doesn't — no marker propagates, no signature change, no third color spreading through the call graph. Async coloring collapses into the purity coloring Lambda already has, plus one keyword at the leaves.
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

Zig attempted colorless async via compile-time dual compilation: every potentially-async function compiled in both sync and async (CPS-transformed) variants, with the compiler propagating "async-ness" through the call graph — *inferred*, with no declaration anywhere. The complexity cascaded — indirect calls, function pointers, the C ABI boundary, doubled compile artifacts — and the feature was **removed from the language** (gone since 0.11, still absent).

The lesson as originally drawn here ("compiler-transform colorlessness is coloring with extra steps — stackful or nothing") motivated the v1 fiber commitment. The **refined lesson** (2026-07-08, see §2.6 and §4.2): what actually failed was the *unselective, ABI-exposed* transform in an **AOT systems language** — stable calling conventions to preserve, function pointers whose async-ness is unknowable across compilation units, generics dual-compiled into artifact explosions. §4.2.3 examines which of those preconditions Lambda has (almost none), and K2-R adopts a *selective* transform where Zig's failure factors are absent; the fiber design remains the reference alternative (§4.2.4).

### 2.4 BEAM — the semantic model for levels 2/3

Per-process heaps and stacks; messages **copied** between processes — with one exception: **large binaries are refcounted and shared**, because they're flat and immutable. Cooperative scheduling by reduction counting. Links, monitors, and supervision trees turn isolation into fault tolerance ("let it crash"). Distribution uses one wire format (ETF).

**Lambda takes:** copy-by-default messaging with flat-immutable sharing as the exception (§5.2 — BEAM's binaries generalize to Lambda's typed arrays/strings/buffers); exit notifications as the failure surface (§5.3); Mark as the wire format (Lambda already has what BEAM had to invent). **Lambda learns from BEAM's scar:** unbounded mailboxes are a famous production footgun (memory blowup under slow consumers) — Lambda defaults bounded (O3).

### 2.5 JS Workers / Node — the level-2 surface and the interop constraint

Web Workers are the proven *surface* for isolate-style threading (isolated heap, `postMessage`, structured clone ≈ Item copy). Node's mistake is the one K4 avoids: `Worker` and `child_process` are unrelated APIs for the same semantic idea. The binding constraint JS imposes on level 1: **run-to-completion** — a JS frame may only yield at its own `await`/job boundaries, never because Lambda's scheduler decided to. This forces (and justifies) the one-loop-per-context design in §4.2. Ruby's Ractors independently confirm the sharing rule: only deep-frozen (immutable) objects are shareable across Ractors.

### 2.6 Async-v2 prior art — who else declared at the leaf and hid the callers? (added 2026-07-08)

No mainstream language ships the K2-R combination — **leaf-declared `async`, unmarked awaiting callers, compiler state-machine transform** — but it has three close relatives:

| Language | Callers marked? | Parked state lives in | Fate |
|---|---|---|---|
| **Zig 0.5–0.10** | no — asyncness *inferred*, even at the leaves | compiler-generated frames (CPS-ish) | **withdrawn** — the closest shipped attempt |
| **Koka / Eff / Links / Unison** (algebraic effects) | no — effect *inferred in types*, invisible in syntax | selective CPS / evidence-passing translation, heap | shipped, research-tier; the theoretically mature version |
| **Kotlin** | **yes** — `suspend` must propagate to every caller | compiler state machines | thriving; the *inverse* of K2-R |
| OCaml 5, Java Loom, Go, Lua/Ruby/PHP fibers | no marking at all | **runtime stacks/segments** | thriving — every production colorless language chose stacks |
| JS / C# / Rust / Swift / Python | yes (full coloring) | compiler state machines | thriving, but the ergonomics being escaped |

Three observations:

1. **Zig is K2-R minus the leaf declaration** — it went further (pure inference, no keyword) and collapsed on AOT-specific problems: stable calling conventions, async-unknown function pointers, generics dual-compiled into artifact explosions. Lambda differs on every axis: closed-world whole-program JIT, no stable ABI to preserve, `await` only at Lambda statement positions (never inside C frames), and the `fn`/`pn` split exempting most of the call graph and most HOF indirection. The leaf declaration also removes Zig's inference-surprise: the may-await closure is *seeded explicitly*.
2. **Koka proves the idea sound — and Lambda is accidentally close to it.** In effect-system languages, "can suspend" is an inferred effect row, never written at call sites; the compiler does a **selective CPS transform over exactly the effectful closure** (Leijen's *Structured Asynchrony with Algebraic Effects* did async precisely this way). K2-R is that design, pragmatized: `async` at the leaf ≈ declaring the effect, O9 propagation ≈ effect inference, the state-machine transform ≈ selective CPS. Lambda already *has* a coarse effect system — `fn`/`pn` is a one-bit effect row; may-await is the second bit. Caveat: no mass-market language has taken this road; Lambda would be first-to-production, not following.
3. **The two production camps split exactly on this question.** Everyone wanting unmarked callers at scale (OCaml 5, Loom, Go, fibers) paid with runtime stacks; everyone choosing compiler state machines paid with visible color. Kotlin is the sharp data point: JetBrains *could* have inferred `suspend` and deliberately refused, arguing suspension points must be visible at call sites. Two things blunt that argument for Lambda: Loom then hid suspension completely and the Java ecosystem accepted it; and Lambda readers of an `fn` know *statically* that nothing suspends — more guarantee than Kotlin readers ever had. Positioning line: **inferred-transitive suspension à la Koka's effect inference, compiled à la Kotlin, declared à la neither — enabled by the `fn`/`pn` split doing the visibility work that Kotlin's `suspend` keyword does.**

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

### 4.1 Language surface (revised 2026-07-08 — K2-R)

```lambda
async pn fetch(url: string) -> response^ { ... }   // declared suspension source (leaf)

pn handle(req) -> page^ {                          // NOT marked — and never needs to be
    let r = await fetch(req.url)^                  // awaiting does not color handle
    render(r)
}
// handle's callers are equally unmarked; suspension propagates invisibly (may-await closure)
```

- **`async pn` declares a suspension source** — a `pn` built on asynchronous host operations, whose call site yields an awaitable. This is the *only* place the keyword appears.
- **`await expr` is a `pn`-only operation.** Using it in an `fn` is a compile error (K1). A `pn` that awaits does **not** become `async` — it stays invisible to its callers: same declaration, same call syntax. Colorless among callers (K2-R); the color exists only inside the compiler (§4.2.3).
- Awaitable things: `async pn` calls, async host operations (`io.*`, timers, net), spawned isolates' results and `receive` (levels 2/3), JS Promises (§4.5), and level-1 task-scope children (O1). Typing of an *unawaited* `async pn` call (a task/handle value) is O6.
- Error integration: awaiting a failed operation yields the error as a value — `await` composes with `T^E` and `^`-propagation exactly like any other `pn` call. No new error channel (extends J3).
- Structured scopes (task groups with scope-bound children, cancellation on scope exit — the Trio/Kotlin/Swift consensus) are the intended surface for spawning *within* level 1; syntax and semantics are open (O1, O2). Note the design adjacency to the `defer`/`with` resource-cleanup gap (features doc §3.1) — same construct family, design together.

### 4.2 Implementation — the suspension problem and the adopted design (K2-R)

#### 4.2.1 The problem lives in the awaiter, not the declared source

At the moment `handle()` awaits, 40 frames deep in `main → serve → handle → …`, the thing that must survive is the **awaiter's call chain**: every frame's locals, spilled registers, and return addresses, laid out contiguously on the native stack. "Resume later" means all of it still exists at valid addresses. Declaring `async` on the *leaf* changes nothing about this — the frames needing preservation belong to the **unmarked awaiters** all the way up.

Two suspended call chains cannot interleave on one contiguous stack (stacks are LIFO: task B's frames pushed above parked task A's bury A until B fully unwinds). So parked state must live somewhere, and there are exactly three exits:

1. **Compile the frames away** — rewrite suspendable functions so locals live in heap objects with numbered resume points (state machines). The rewrite is traditionally the color (JS/Rust/Kotlin/C#) — *unless it is hidden in the compiler* (K2-R, §4.2.3).
2. **Evacuate the frames** — copy the suspended stack segment to the heap and back (Loom). Requires complete frame knowledge: C and MIR frames legitimately hold *interior pointers into the stack* (`&local` passed to a callee); copying without Go-grade precise maps + pointer rewriting is UB. Not available to Lambda.
3. **Give each task its own stack** — fibers (§4.2.4). Park = save SP + registers; frames never move.

#### 4.2.2 Rejected: blocking and nested-loop awaits

- **Thread-blocking `await` self-deadlocks under K3**: the awaiter blocks the context's only thread — but the awaited operation's completion is *delivered by the loop on that thread*. The loop can never run; the await never completes.
- **Nested-drain `await`** (run the loop recursively inside the await) is the infamous recursive-event-loop footgun: tasks resume in LIFO burial order (a completed task can't resume while later arrivals sit above it on the stack), stack depth grows unboundedly under load, and JS jobs run mid-`pn` in violation of run-to-completion.

#### 4.2.3 ADOPTED (2026-07-08, K2-R): may-await state-machine compilation

The color goes **into the compiler**, along exactly the calls that need it:

- **The may-await closure**: seeded by `async` declarations and `await` expressions, propagated along call edges (a `pn` calling a may-await `pn` is itself may-await). Indirect `pn` calls (through closures/HOFs) are conservatively may-await; **`fn`s are exempt by construction** — and Lambda's idiomatic HOF surface (pipes, `map`, `where`) takes `fn` arguments, so the conservative case is rare in practice. This is O9, upgraded from inference to declared-and-checked.
- **The transform**: every `pn` in the closure is compiled as a resumable state machine — split at each `await` and at each call to a may-await `pn`; live locals hoisted to a heap frame — the same lowering LambdaJS Phase 6 already performs for JS `async` functions (`js_mir_function_class_lowering.cpp`).
- **The calling convention**: calls *to* may-await `pn`s return `value | suspended`; callers in the closure propagate suspension by parking their own state machine. Calls to sync `pn`s and all `fn`s are completely unchanged.
- **Scheduling**: unchanged from v1 — one loop per context (libuv, K3/K10), cooperative suspension at explicit points, `fn` call trees are suspension-free regions.

Why this beats fibers *for Lambda specifically* (the four factors):

1. **Closed world.** Each script is whole-program at JIT time — the transitive transform that broke Zig (stable ABIs, dual-compiled generics) is just another whole-program analysis here, next to purity inference.
2. **Awaits never occur inside C frames.** `await` is a Lambda statement position; a C runtime helper or native-module frame never spans one. The case where fibers were uniquely capable — suspending with foreign frames mid-stack — is *already forbidden* by §4.4 and the JS membrane rule. Fibers' superpower is one the design had already renounced.
3. **The machinery half-exists.** Phase-6 state-machine lowering is the same transform, already debugged against the JS suites.
4. **It decouples level 1 from G1.** A parked fiber is a native stack GC must scan; a parked state machine is an **ordinary heap object** — rooted and traced like any value, no stack scanning, no new GC surface. The scariest dependency in the plan dissolves (§4.3).

Honest costs: two `pn` calling conventions (suspendable vs plain) and a suspension-check branch at may-await call sites; conservative transformation at indirect `pn` calls; transform complexity lands in `transpile-mir`; debugging shows chopped stacks (like JS async today) instead of real ones.

#### 4.2.4 Reference: the stackful-fiber design (v1, superseded but viable)

Kept as the documented alternative — it would be revived if the transform proves worse than expected, and levels 2/3 are unaffected either way. The design was the cheap slice of Go/Loom: **fixed mmap'd fiber stacks, a ~30-line context switch, park/resume at `pn` await points.**

- **Fiber stacks:** reserve ~1 MB virtual per fiber via `mmap`, lazily committed, guard page for overflow. A parked fiber holds a few KB of committed pages. No growth, no copying, no pointer rewriting — deliberately sidesteps Go's precise-map relocation machinery. Cost ceiling: thousands of fibers, not millions — which matches the domain.
- **Context switch:** hand-rolled callee-saved registers + SP per architecture (x86-64 SysV, AArch64); explicitly **not** `ucontext` (sigmask syscall per switch). The libco/Boost.Context design.
- **MIR JIT unchanged**; JIT'd frames are ordinary frames on the fiber's stack — the "zero compiler transform" property that was the original decisive argument, before factors 2–4 above outweighed it.
- **Its GC cost (why it lost):** parked fiber stacks are GC roots — v1 would conservatively scan committed portions (Go pre-1.4 precedent), precise scanning arriving only with G1; level 1 would be **gated on G1**, and more live stacks worsen the blanket-rooting issue (`Lambda_GC_Root_Issue.md`).

#### 4.2.5 The head-to-head that decided it

| | Fibers (§4.2.4, superseded K2) | May-await state machines (K2-R, adopted) |
|---|---|---|
| Compiler work | none | transform over may-await set in `transpile-mir`; two `pn` call conventions |
| Runtime work | per-arch asm switch, mmap stacks, scheduler | scheduler only |
| Parked-state memory | committed stack pages (few KB each) | heap frames ∝ live locals (smaller) |
| GC interaction | **parked-stack scanning; level 1 gated on G1** | **none new — ordinary heap objects** |
| Indirect `pn` calls | free | conservatively transformed |
| Native frames across await | forbidden by rule (§4.4) | impossible by construction |
| Debugging | real stacks in a debugger | chopped stacks (like JS async today) |
| Surface colorlessness | full | full (color hidden in compiler; `async` at leaves only) |

The decisive rows are GC interaction (factor 4 — G1 was the scariest dependency in the plan) and native-frames (fibers' unique capability was already renounced by §4.4 and the membrane rule). Fibers keep two real advantages — debugger-friendly stacks and free `pn` indirection — judged worth less than de-risking G1 and skipping runtime assembly.

### 4.3 GC interaction (revised under K2-R)

- Parked state = **ordinary heap objects** (state-machine frames holding boxed Items). They are rooted, traced, and collected like any other value — **no parked-stack scanning exists, and level 1 is no longer gated on G1**.
- G1 (`Lambda_GC_Root_Issue.md`) remains the top runtime-honesty work item for the *running* frame's rooting — unchanged by this design, neither worsened nor gated. The fiber alternative (§4.2.4) would reinstate the gate; that asymmetry was decision-relevant (factor 4).

### 4.4 Native frames — the module-ABI clause (unretrofittable; write into JubeHostAPI v1)

- A task **must not suspend while a `JubeHostAPI` frame is on the stack**, unless the module declares that entry point await-safe. Under K2-R this is *impossible by construction* (a state machine can only suspend at Lambda-level await points, never inside a C frame) — under the fiber alternative it was a rule to enforce (Loom's pinning problem, Go's cgo case). Either way the clause stays in the ABI: it documents the invariant modules may rely on.
- A host call that genuinely blocks (sync file I/O in a module, a C library call) **detaches the OS thread** Go-style: the context's loop continues on another thread, the blocking call completes on the detached one. v1 may simplify to "blocking host calls pin the context" with a documented list, but the ABI *clause* — modules must declare blocking/await-safety — goes in v1, because it cannot be retrofitted once modules exist.

### 4.5 JS bridge

- Lambda `pn` awaiting a **JS Promise**: park the task (suspend its state machine); resume on settlement. Rejection surfaces as an **error value** (`T^E`) — no exception crosses the boundary (J3).
- Lambda async operations surface to JS as ordinary **Promises**; a JS `await` of a Lambda operation is just a promise await.
- One loop (K3) makes the bridge mechanical: promise settlement and fiber resume are entries on the same queue. No cross-loop marshalling exists because there is no second loop.

### 4.6 JS coexistence — two continuation mechanisms, one reactor (K10)

The question "should JS async migrate to fibers, or keep libuv?" dissolves into three layers with different answers. Grounded in the current code: LambdaJS's loop is already **libuv** (`js_event_loop.h` — drain via `uv_run` + microtask flush), and JS `async/await` is already lowered to **state machines** in the MIR compiler (`js_mir_function_class_lowering.cpp`, Phase 6 — resume slots sized by yield+await count).

**Layer 1 — the reactor: unify; libuv *is* the K3 loop.** libuv is the thing that waits (kqueue/epoll/IOCP); fibers and state machines are two *continuation mechanisms* hanging off it. Lambda `await` = start the uv operation, park the fiber; the uv callback resumes it — Go's netpoller with libuv playing netpoller. Two reactors in one thread is a broken design (who blocks on poll?); libuv already solved the portability matrix. Payoff: `js_fs`/`js_net`/`js_http`/timers become **shared uv-backed services with two thin fronts** — JS gets callbacks/promises, Lambda gets park/resume — instead of a parallel Lambda I/O stack for O8. Dovetails with the `node-*` native-module carve-out (module-design POC 2): the uv-backed services are those modules.

**Layer 2 — JS async semantics: do NOT migrate.** JS stays colored, on its existing state machines:
1. **JS coloring is spec, not implementation.** Microtask timing is observable, tested behavior (`await` yields to the job queue even for resolved values; `Promise.resolve().then` ordering; run-to-completion per job). Re-implementing JS `await` as fiber parking would still have to reproduce job ordering exactly — no semantic simplification exists, only risk.
2. **The mechanism is built and green** (editor JS suite, node baseline). Rewriting a working spec-compliant lowering for internal uniformity is negative-value work.
3. **Cost profile:** JS allocates promises in huge numbers; a state machine is a small heap object, a fiber is a mapped stack. Fiber-per-async-call is strictly heavier for the language that awaits the most.

Asymmetry is not incoherence: each language gets the mechanism its semantics demand, on one shared reactor. *(K2-R postscript: with Lambda also on state machines, the two languages now share one mechanism family — the Phase-6 lineage — differing only in that JS's color is spec-visible and Lambda's is compiler-internal. The layer-2 reasons for not touching JS's own lowering stand unchanged.)*

**Layer 3 — the membrane rule.** What happens when JS synchronously calls a Lambda `pn` that awaits? Suspending it would pause a JS job mid-execution while other jobs run — other JS code would observe state mid-job, violating run-to-completion. The rule:

> **A may-await `pn` invoked from JS is dispatched as a fresh task and returns a Promise to JS immediately.** Colorlessness is a Lambda-internal property; at the JS membrane, awaiting `pn`s *appear* colored (promise-returning). The boundary adapter does the coloring — never the Lambda source.

Two supporting obligations (open items):
- **May-await closure (O9):** under K2-R this is no longer a side analysis but the transform set itself — seeded by `async` declarations + `await` expressions, propagated along call edges, conservative at indirect `pn` calls. The JS-facing wrapper generation consumes the same set.
- **Resume ordering (O10):** when a settled promise resumes a Lambda task, the resume is enqueued **after the current job's microtask drain** (macrotask-position), so JS job-queue invariants hold. The precise interleaving of task resumes vs. uv callbacks vs. microtask flushes must be specified once, in the loop.

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

- An isolate = its own Lambda context: own heap, own task scheduler, own event loop (a level-2/3 isolate is "a context on its own thread/process" — the level-1 substrate, replicated).
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

### 5.5 Scheduling topology — no task migration, ever (K7)

- Each isolate owns exactly one OS thread (level 2) or process (level 3), running its own task scheduler + event loop. **Tasks never migrate between threads** (true for state machines and for the fiber alternative alike). Parallelism comes from having many isolates, not from work-stealing within one.
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
| Growable/relocatable stacks | requires precise-map + pointer-rewrite machinery (the hard 40% of Go) for a scale (1M tasks) the domain doesn't need | heap state machines (K2-R); fixed mmap'd stacks in the fiber alternative |
| Shared mutable memory (incl. JS `SharedArrayBuffer`/`Atomics`) | the entire design rests on immutable sharing; C4 | flat read-only sharing + messages |
| *Unselective* whole-program CPS with an exposed dual ABI | Zig's actual failure mode (§2.3, §2.6): AOT stable conventions, async-unknown function pointers, dual-compiled generics | the **selective** may-await transform (K2-R) — confined to the may-await closure, invisible at the surface, closed-world |
| Blocking / nested-loop `await` | self-deadlock under K3 (completion needs the loop the awaiter blocks); recursive-drain = LIFO burial + unbounded stack + run-to-completion violations (§4.2.2) | suspendable state machines on one loop |

---

## 7. Guest languages on the concurrency substrate

- **JS/TS:** Promises/`async-await` run unchanged — colored, on their existing state machines, over the shared libuv loop (K10, §4.6). Calls into may-await `pn`s auto-promote to Promise at the membrane (§4.6). `Worker` maps to `spawn {isolation:'thread'}` with structured clone = Item copy. `SharedArrayBuffer` excluded (§5.2).
- **Python/Ruby/Bash (Jube):** guest threading APIs (`threading`, `Thread`, subshells) map — under the J5 dialect banner — onto isolates or are documented-unsupported. `asyncio`/Fiber interop is a per-guest mapping table, same family as the G3 error-mapping tables. No GIL emulation: guests get real isolates or nothing.
- Native modules: §4.4 clause (declare blocking/await-safety) + G2 (rooting across the ABI) are the two JubeHostAPI v1 obligations this design adds.

---

## 8. Decision ledger

**Confirmed (K):**

- **K1** — `fn` never suspends, never observes concurrency; `await` is `pn`-only. No third color: async coloring collapses into the existing purity color. *(user-confirmed)*
- **K2** *(2026-07-06 — superseded by K2-R below)* — original commitment: colorless-among-`pn`s via **stackful fibers** (fixed mmap'd stacks, ~30-line switch, MIR JIT unchanged). Retained as the documented reference alternative (§4.2.4); would be revived only if the K2-R transform underdelivers.
- **K2-R** *(2026-07-08)* — Level-1 async is **declared at the leaves, colorless at the callers, compiled selectively**: `async pn` declares suspension sources; `await` is legal in any `pn` without marking it or its callers; the compiler computes the **may-await closure** (seeded by `async` declarations + `await` expressions, propagated along call edges, conservative at indirect `pn` calls, `fn` exempt) and compiles exactly that set into resumable state machines (Phase-6 family) with a `value | suspended` calling convention. Parked state = ordinary heap objects → **no parked-stack scanning; level 1 decoupled from G1** (§4.2.3 factors 1–4). *(user-confirmed)*
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
- **O9** — **May-await closure** *(upgraded by K2-R from side-analysis to the transform set itself)*: seeded by `async` declarations and `await` expressions, propagated along call edges, conservative at indirect `pn` calls (`fn` exempt by construction). Consumed by the K2-R transform, JS-facing wrapper generation (§4.6), and diagnostics ("this `pn` suspends because it calls …"). Remaining to spec: typing of an unawaited `async pn` call (ties to O6) and the exact conservatism rule for `pn`-valued closures.
- **O10** — **Loop ordering spec**: fiber resumes enqueue at macrotask position (after the current job's microtask drain); one written spec for the interleaving of uv callbacks, microtask flushes, and fiber resumes.

---

## 9. Dependencies & sequencing

1. **G1 no longer gates level 1** (K2-R): parked state machines are ordinary heap objects — level 1 adds no new GC surface. G1 (`Lambda_GC_Root_Issue.md`) remains the top runtime-honesty work item in its own right, and would return as a level-1 gate only if the fiber alternative (§4.2.4) were revived.
2. **JubeHostAPI v1 must include** the §4.4 native-frame clause (blocking/await-safety declaration) and the G2 rooting-across-ABI clause **before modules proliferate** — both are unretrofittable.
3. Suggested build order: **level 0** (small, isolated, immediately useful for the typed-array/image workloads) → **level 1** (compiler track: may-await closure + state-machine lowering in `transpile-mir` + suspendable `pn` convention; runtime track: scheduler + uv integration — unblocks async I/O and the Radiant UI story) → **level 2** (isolate = context-on-a-thread; level 0 then migrates onto it) → **level 3** (same API over processes + Mark wire).
4. The per-guest concurrency mapping tables (§7) join the G3 error-mapping tables as module/ABI-spec work items.

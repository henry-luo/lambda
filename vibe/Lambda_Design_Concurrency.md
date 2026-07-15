# Lambda Concurrency Design — Colorless Concurrency: the Evolution to v3 (One Keyword, Two Tiers)

**Status:** **v3 adopted** (§10) — v1 and v2 preserved below as design history
**Date:** v1 2026-07-06 (fibers) · v2 2026-07-08 (declared-`async` + K2-R state machines) · **v3 2026-07-08 (current): one keyword `start`, two tiers, threads on the roadmap**
**Context:** realizes decision **J4** (BEAM-style concurrency, stated explicitly) from the Jube runtime ledger — Part 1 of `Lambda_Semantics_Features.md`. Addresses the #1 structural gap from the six-language feature comparison (Part 3 of the same doc).

**Reading guide:** §1–§9 document v1 and v2 as they were designed — kept intact because the reasoning is the asset (why fibers, why state machines, what Go/Loom/Zig/BEAM each teach). Neither is the final design. **The adopted design is v3, §10**, which supersedes the four-level model with a two-tier one and records the disposition of every earlier decision in §10.9.

---

## 1. The model at a glance

*[v1/v2 — superseded by the two-tier model of v3, §10.3. Kept for reference.]*

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

*[v1/v2 — in v3 this generalizes into internal, invisible parallel-`fn` execution (§10.6 Stage A); the determinism principle and `fn`-only rule survive unchanged. The float-reduction caveat is new in v3 (open item O-B).]*

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

*[v1 (fibers, §4.2.4) and v2 (declared-`async` + K2-R state machines, §4.1–4.2.3). In v3: the **K2-R state-machine mechanism survives** (§10.5), but the surface changes — the `async` declaration is **removed** (§10.2), `await` becomes the `wait()` builtin, and `run`/spawning becomes the `start` keyword. §4.4–4.6 (native frames, JS bridge, K10 reactor) carry into v3 unchanged except as noted in §10.7.]*

### 4.1 Language surface (v2 — revised 2026-07-08 — K2-R)

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

*[v1/v2 — superseded by v3 §10.3: the worker-thread tier (level 2) is dropped as a user-visible concept; processes become simple OS child processes launched by the same `start` keyword. What survives into v3: the uniform-handle idea (awaitable → `T^E`, message target), the memory model (§5.2 — copy messages, share flat immutables, Mark as process wire format), the failure-as-values model (§5.3), bounded delivery (§5.4), and `select`. The mailbox-vs-channel question was reopened in v3 and resolved as mailbox (K20, §10.8).]*

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

*[v2-era table. v3 dispositions: the `go`-spawn exclusion softens — v3 adopts a Go-inspired single spawn keyword (`start`) but keeps structured intent; the channels exclusion was reopened and resolved as mailbox (K20) — the original instinct reinstated in evolved form; everything else stands. See §10.9.]*

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

## 8. Decision ledger (v1/v2)

*[Historical ledger. Every entry's v3 disposition — kept / revised / superseded / reopened — is recorded in §10.9.]*

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

## 9. Dependencies & sequencing (v2 — superseded by §10.10)

1. **G1 no longer gates level 1** (K2-R): parked state machines are ordinary heap objects — level 1 adds no new GC surface. G1 (`Lambda_GC_Root_Issue.md`) remains the top runtime-honesty work item in its own right, and would return as a level-1 gate only if the fiber alternative (§4.2.4) were revived.
2. **JubeHostAPI v1 must include** the §4.4 native-frame clause (blocking/await-safety declaration) and the G2 rooting-across-ABI clause **before modules proliferate** — both are unretrofittable.
3. Suggested build order: **level 0** (small, isolated, immediately useful for the typed-array/image workloads) → **level 1** (compiler track: may-await closure + state-machine lowering in `transpile-mir` + suspendable `pn` convention; runtime track: scheduler + uv integration — unblocks async I/O and the Radiant UI story) → **level 2** (isolate = context-on-a-thread; level 0 then migrates onto it) → **level 3** (same API over processes + Mark wire).
4. The per-guest concurrency mapping tables (§7) join the G3 error-mapping tables as module/ABI-spec work items.

---

# v3 — THE ADOPTED DESIGN (2026-07-08)

## 10. One keyword, two tiers, threads in the future

### 10.1 How the design moved: v1 → v2 → v3

- **v1 (fibers):** full colorlessness via stackful fibers — own stacks so unmodified callers can suspend. Decisive argument at the time: zero compiler transform.
- **v2 (K2-R):** the user lowered the colorlessness requirement (`async` declared at leaves, callers unmarked), which exposed that a *selective* state-machine transform gets the same surface with parked state as ordinary heap objects — **decoupling level 1 from G1** and reusing the JS Phase-6 machinery. Fibers demoted to reference.
- **v3 (this section):** three further realizations reshaped the model itself:
  1. **Fibers cannot absorb worker threads.** A fiber has a thread's *anatomy* (own stack) but not its *physiology* (CPU parallelism, heap isolation). All fibers in a context run on one thread; a CPU-bound fiber stalls the whole context. So "fiber ≈ thread" was a false economy — which removed the last argument for fibers and freed the model question from the mechanism question.
  2. **Go answers "coroutines on threads?" with yes — at a price Lambda shouldn't pay.** Goroutines are M:N across threads because Go built a thread-safe-everything runtime (per-P allocator caches, fully concurrent GC with write barriers, cross-thread stack scanning) and made **shared mutable memory the user contract** (data races are the user's problem). BEAM answers the same question differently: **isolated-heap processes** roam across scheduler threads — no shared-heap GC, no races, messages copy. The v3 growth path is BEAM's, not Go's: if many cheap parallel units are ever needed, schedule *isolates* M:N, never fibers (§10.6).
  3. **The user's simplification:** one mechanism to rule them all — concurrent `pn`s (Go-goroutine-inspired) + simple child processes; no separate async/worker-thread/green-process taxonomy. Multicore is a **committed goal** ("today's devices are almost all multi-core"), including internal parallel execution of pure `fn`s.

### 10.2 The surface: one keyword + five builtins

```lambda
pn main() {
    let h1 = start fetch(a)                      // pn call → concurrent task
    let h2 = start fetch(b)                      // both in flight
    let p  = start process("worker.ls", args)    // process spec → OS child process, same keyword
    let r  = wait(h1)^                           // builtin — T^E surfaces here
    send(p, job)                                 // handles are the message target
    let x  = select(h2, p, timeout: 5000)        // first of several (syntax TBD, O4)
}
```

**`start` is the only concurrency keyword in the grammar.** Everything else is a builtin `pn`: `wait(h)`, `send(h, msg)`, `receive()`, `select(...)`, `process(...)`. A **direct call stays a plain call** — `fetch(url)` synchronously (colorlessly) yields the value; it is `wait(start fetch(url))` minus the handle ceremony. Concurrency enters a program through exactly one word.

**Keyword selection (deliberation record):**

| Candidate | Verdict | Reason |
|---|---|---|
| `go` | rejected (user) | branded by Go |
| `spawn` | rejected (user) | mentally linked to processes |
| `run` | rejected | reads synchronous in English ("run this" = do it now); corpus-clean, but the connotation is wrong for fire-and-continue |
| `fire` | rejected | fire-and-**forget** connotation clashes with returning a handle you `wait` on; collides with "firing events" in Radiant |
| `launch` | runner-up | zero corpus conflicts, good track-what-you-launched connotation, mild positive Kotlin echo; slightly long for the `let`/`fn`/`pn` house style |
| **`start`** | **adopted** | exact English semantics ("start the dishwasher" = set in motion, walk away); tier-neutral (start a task / start a process); **must be a contextual keyword** — `start` is a live identifier in the corpus (range params, map keys) |

**`wait`, not `await`** — four stacked reasons: (1) `await`'s *a-* prefix exists to pair with `async`, which v3 removes — orphaned branding; (2) **`start`/`wait` is a natural-language dual** ("start it… wait for it") in the house style; (3) POSIX `wait(2)` means precisely "wait for a child process and collect its status" — forty years of prior art for `wait(p)` on a process handle; (4) **polyglot clarity in Jube**: JS keeps its real `await` (colored keyword, microtask, promise-unwrapping); Lambda's colorless `T^E`-returning builtin deserves a different name so the same word never means two things in one binary. Corpus check: `wait` is unused as an identifier and no `wait`/`sleep` sys func exists — clean.

**`async` is removed from the grammar entirely.** The v2 leaf declaration turned out to have no remaining job:
1. The true suspension leaves are **builtins** (`wait`, `receive`, `sleep`, async `io.*`) — the compiler knows them without declarations; user `pn`s are only ever *transitively* suspending.
2. The one place async-ness still needs declaring is the **native-module boundary** (a module `pn` completing via uv callback) — and module functions already declare signatures in Lambda type syntax; the async bit is one more marker in the FFI contract, not in user grammar.
3. May-await-ness has **no observable consequence inside Lambda** (colorless callers; `fn` cannot call `pn` at all) — so there is no "inference surprise" to protect against (Zig's inference wasn't what failed; its AOT/ABI consequences were).
4. The single real exposure is the **JS membrane**: value-vs-Promise shape would ride inference. v3 rule: **every Lambda `pn` exposed to JS is Promise-returning, uniformly** (Node-style; stable under any refactor). An opt-in `sync` export annotation can come later if tight DOM-handler interop demands it.

Result: colorless is *total*. Plain calls suspend invisibly; `start` is the only visible concurrency; the surface is smaller than Go's (`go` + `chan` + `<-` + `select` as syntax) with none of its races.

### 10.3 The model: two user tiers + one invisible tier

| Tier | Unit | Created by | Parallel? | Isolation | Failure surface |
|---|---|---|---|---|---|
| **Tasks** | concurrent `pn` | `start f(x)` | phase 1: no (one thread/context); Stage B: yes | shared context heap — all immutable values shared free | handle → `T^E` |
| **Processes** | OS child process | `start process(spec)` | yes | OS-grade | handle → `T^E` on exit |
| *(internal)* | parallel `fn` | runtime's discretion | yes (Stage A) | invisible by purity | n/a — deterministic |

- **Processes are simple child processes** — deliberately *not* BEAM green processes: no supervision trees, no links, no distribution, no thousand-process swarms. The handle resolving to `T^E` on exit is the **entire v1 failure story** (it replaces the monitoring layer — a dead child is an error value in the parent, composing with `^`). Recorded caveat: this is *less* BEAM-like, not more — BEAM's "processes" are isolated-heap green processes in **one address space** (level-2-shaped); Lambda chooses OS simplicity first and keeps BEAM-style M:N isolates as the documented escape hatch (§10.6).
- **The worker-thread tier is gone as a user concept.** Compute parallelism comes from internal parallel-`fn` (Stage A) and processes now, and from tasks-on-threads later (Stage B) — never from a user-visible "thread" object.
- **Handles are uniform across tiers**: awaitable (`wait` → `T^E`), message targets (`send`), selectable. Moving work between tiers is changing the operand of `start`, not the verbs around it (the K4 idea, carried forward).
- Communication: **messages + read-only flat immutables** (K5 unchanged: typed arrays/strings/binaries by refcount in-address-space, shm across processes; Mark is the process wire format). Whether messaging is mailbox-shaped or channel-shaped is open (O-A).

### 10.4 The soundness rule: thread-agnostic task semantics (the capture rule)

> **A `start` closure must not capture `var`s by reference.** Compile error (or explicit snapshot). Tasks communicate through messages and immutable flats — never through captured mutable state.

Lambda `pn` closures *can* mutate captured vars today (`proc_closure_mutation` tests), so `start` is deliberately stricter than closure creation. Consequences:

- **Thread count becomes semantically unobservable.** No program can distinguish tasks multiplexed on one thread from tasks on eight — so Stage B (threads) is a pure runtime upgrade, zero user-code changes. Without this rule in v1, the first program sharing a `var` between tasks forecloses multicore forever. **Unretrofittable — ships with `start` or never.**
- **This is what Go couldn't have.** Go's contract *is* shared mutable memory, so it bought M:N with a thread-safe-everything runtime and user-visible data races (plus a race detector as apology). Lambda's contract is values + messages: **Go's ergonomics minus Go's races** — C4 finally paying its concurrency dividend.
- Nondeterminism remains confined to `pn` (message arrival, task interleaving); `fn` remains deterministic under any schedule. The C1–C12 core survives untouched.

### 10.5 Mechanism: K2-R state machines confirmed — precisely *because* of the threading goal

v3 keeps v2's compilation strategy (may-await closure → resumable state machines, Phase-6 family, `value | suspended` convention), with the closure now seeded by **builtins + native-module declarations only** (no user `async`). The threading goal *strengthens* this choice:

- A parked state machine is a heap object; **resuming it on any pool thread is a function call** — exactly how Kotlin coroutines, C# tasks, and Rust/tokio do M:N today. Proven, boring, portable.
- M:N over **fibers** is the road only Go and Loom ever completed: live native stacks migrating across threads (TLS discipline, cross-thread stack scanning, pointer pinning). Fibers are the *less* thread-portable mechanism, contrary to first intuition — their own-stack property buys unmodified-caller suspension, not thread mobility.
- The fiber design (§4.2.4) remains the documented fallback, revivable only if the transform underdelivers — but note it would reinstate both the G1 gate and the harder Stage-B road.

### 10.6 The multicore roadmap: Stage A (fn, fork-join) then Stage B (pn tasks, M:N)

**Committed goal (user): real multi-core utilization, including for pure `fn`s — internal, never user-visible.**

**Stage A — parallel `fn`, fork-join (the cheap 80%).** `fn` is pure and can never suspend (K1), so it fits the Cilk-style fork-join model without touching the GC architecture:
- Worker threads allocate in **private scratch arenas**; results copied/promoted into the main heap at the join.
- **GC blackout during a parallel region** (regions are bounded — a par-map batch, a large `ArrayNum` op): no safepoint protocol, no concurrent marking, and the compacting GC can't move inputs mid-region.
- **The real Stage-A work item is the runtime-globals audit** — "pure at the language level ≠ pure at the runtime level": an `fn` building a map touches the **shape registry**; symbols touch the **namepool**; strings touch interning; boxed numerics touch the allocator. Each needs a lock, a per-thread side table merged at join, or pre-warming. An audit-and-fix list, not an architecture change — but it is where Stage A's effort actually goes.
- Heavy compute in Lambda *is* `fn` (image ops, math, transforms — the typed-array workloads), so Stage A delivers most of the multicore value at a fraction of the risk. Old level 0 (`par` map) becomes library sugar over this machinery.
- One semantic caution — **open item O-B**: parallel *map* is always deterministic; parallel *reduction over floats* is not (chunked association ≠ sequential left-fold, and the MathLive corpus work proved float-accumulation order is observable). Policy needed: fixed tree order (deterministic run-to-run, documented ≠ sequential) vs. exact-types-only auto-parallelization (int/decimal parallel, float reductions stay sequential by default).

**Stage B — `pn` tasks M:N over a thread pool (the expensive 20%).** The full bill Go paid: thread-safe allocator (per-thread nurseries), concurrent or stop-the-world-multi GC, atomic COW refcounts (a tax that lands on single-threaded code too — the nogil lesson). Semantics are **already ready** thanks to the capture rule; state-machine frames resume on any thread by construction. No user-visible change when it lands — that's the entire point of §10.4.
- **Recorded alternative if Stage B stalls:** BEAM's road — many cheap *isolated-heap* units scheduled M:N over scheduler threads (at most one scheduler runs a given isolate at a time; even V8 permits thread migration under that rule). Compatible with C4's economics; requires making contexts cheap rather than making the heap concurrent.

### 10.7 Unifying with JS async at the implementation level (K17)

Both languages' async decomposes into five layers; **three unify, two must not**:

| Layer | JS today (Phase 6) | Lambda v3 | Unify? |
|---|---|---|---|
| 1. Function-splitting transform | split at `await`/`yield`, hoist live locals, state dispatch | split at async-builtin calls *and* calls to may-await `pn`s | **Yes — extract a neutral MIR "resumable function" utility** |
| 2. Resume frame | heap object: state idx + slots (+`this`/`arguments`) | heap object: state idx + Item slots | **Yes — common layout; JS adds fields** |
| 3. Resumption protocol | promise reactions, microtask, resume-with-**throw** | scheduler resume at macrotask (O10), resume-with-**`T^E` value** | No — thin per-language drivers |
| 4. Calling convention | always-Promise (allocation per call) | **`value \| suspended`** — zero allocation when nothing suspends | No — Lambda's fast path is the perf edge; don't unify downward |
| 5. Reactor/loop | libuv + job queue | same loop | **Already unified (K3/K10)** |

Notes that make the split principled: Lambda has *more* split points (suspension flows through calls) but *cheaper* calls (the Kotlin `COROUTINE_SUSPENDED` trick); Lambda's resumption is *simpler* (no abrupt-completion injection — errors are values in a slot, `^` does the rest), so exception-region bookkeeping in the shared skeleton is an optional feature only the JS driver requests. **Interop dividend:** once both park as "heap frame + `resume(state, value)`," the §4.6 membrane is glue — a promise reaction re-enqueues a Lambda frame at macrotask position; a Lambda handle completion resolves a Promise. And Stage B applies to both languages' frames for free.

**Sequencing (protect what's green):** build the Lambda transform in `transpile-mir` *following* Phase 6's pattern as an independent second implementation; extract the common core only after two working clients exist, gated on the JS suites (1931 editor tests, node baseline). Extraction driven by two concrete users, not anticipation. **Third-client bonus:** the same resumable-function machinery is what Lambda generators / lazy stream producers (D9–D12) would need — strengthening the eventual extraction case.

### 10.8 Open items

- **O-A — Mailbox vs. channel: DECIDED (K20, 2026-07-08) — mailbox (actor model), with two refinements.** The essence that framed the choice: *a mailbox belongs to a process; a channel belongs to no one* (actor vs. CSP). Comparison that informed the decision:

  | | Mailbox (BEAM/actor) | Channel (Go/CSP) |
  |---|---|---|
  | Addressing | send **to a task/process** (its handle) | send **into a pipe**; receive-end holder gets it |
  | Cardinality | one per task; all senders merge | many; first-class values, N:M (shared channel = work queue free) |
  | Typing | heterogeneous; sort at receive | `chan(T)` — typed pipe |
  | Receive | selective receive (match within queue — powerful, but the O(n²) scan trap) | strict FIFO + `select` *between* channels (can't express the scan trap) |
  | Send | always async, never blocks (distribution-friendly; unbounded footgun) | bounded — blocks when full (backpressure built in; careless topologies can deadlock) |
  | Lifecycle | dies with owner; monitors notify | outlives tasks; explicit `close`, end-of-stream |
  | Distribution | location-transparent (BEAM's superpower) | needs explicit plumbing across processes |

  Channels would have obligated: a close-semantics rule (who closes; multi-sender), dead-peer signaling, and cross-process endpoint plumbing. Mailboxes fit the simplified no-distribution world and the handle model — `send(h, msg)` is mailbox-shaped already. **The decision (K20a–e):**

  - **K20a — Handle = address; no channel type.** One mailbox per task/process; the handle returned by `start` is the send target. One noun in the API — no second messaging concept. Bootstrap details: a `self()` builtin (hand your own address to others) and the parent handle reaching the child (implicitly, or via start arguments); handles are sendable *inside messages* so topologies can form — freely in-context; **cross-process v1 = the pre-wired parent↔child pair only** (forwarding a third process's handle would require OS-level routing — deferred until real demand). *(user)*
  - **K20b — N:1 by design; N:M is a redesign smell.** One task does one thing well; a would-be N:M topology is split into N:1 stages. The one workload that genuinely wants M receivers — the shared work queue — is expressed as an explicit **dispatcher task** (receive jobs, forward round-robin): one extra task, clearer than Go's implicit competition, and the 30-year BEAM idiom. Documented pattern, not a gap. *(user)*
  - **K20c — Heterogeneous messages; FIFO-head receive; NO in-queue selective receive.** Messages are any Lambda value (heterogeneity is what Lambda embraces), dispatched with the existing `match`. But `receive()` always yields the **oldest message** — there is no skip-and-leave-in-queue: BEAM's selective receive scans the queue, takes the first match, and leaves the rest, which is the source of its most notorious performance trap (unmatched messages accumulate; every receive re-scans; O(n²) meltdown that Erlang needed a special compiler optimization to mitigate). Under FIFO-head + `match`, unhandled kinds are an explicit user choice (ignore/error), never silent queue residue — **the trap is unrepresentable**. True selective receive = possible future opt-in if RPC-reply patterns demand it. *(user + refinement accepted)*
  - **K20d — Async bounded send; backpressure as an error value.** `send(h, msg)` **never blocks** and returns `ok^E`: a full mailbox surfaces as an error *value* the sender handles (retry, drop, escalate) — not blocking, not silent unbounded growth. The bounded-delivery non-negotiable holds (the BEAM footgun stays dead), and backpressure becomes visible in the type system — which no other actor runtime offers. **Future opt-in blocking variants, deferred until pressing** (surface TBD when needed): `wait(send(...))` — send returning a delivery handle — or `send(h, msg, sync: true)`. *(user + refinement accepted)*
  - **K20e — Delivery outlives the sender; ordered end-of-stream (the signal-ordering guarantee).** `send` enqueues into the *receiver's* mailbox, so sent messages survive sender termination automatically; for processes the runtime drains the pipe to EOF. Spec guarantee: **a task/process's termination (observed via `wait(h)` / `select(..., h)`) becomes visible only after all messages it previously sent have been enqueued** — per-sender FIFO, termination last. End-of-stream is therefore just handle completion carrying the final `T^E`: no sentinel messages, no `'done'` conventions, no auto-injected system messages polluting the mailbox. Streaming (select over mailbox + child handle), request-reply (reply-to handle inside the message), and dispatcher pools all work with zero additional machinery. *(user requirement, formalized)*

  Net: the entire messaging layer is `send`/`receive`/`select` + handles + two ordering guarantees (per-sender FIFO; termination-after-sends). K8's original mailbox-plus-select instinct is thereby reinstated in evolved form — bounded, handle-addressed, trap-free.
- **O-B — Float-reduction policy: DECIDED (K19, 2026-07-08) — pairwise-by-spec.** Builtin numeric reductions (`sum`/`avg`/`prod`/`variance`/dot; `min`/`max` join for NaN consistency) are *defined* as a fixed, size-derived tree order: base blocks of fixed size with a fixed stride pattern (matching SIMD lane layout), blocks combined pairwise in index order; the tree depends **only on n** — never on thread count, timing, or backend. Scalar, SIMD, and Stage-A parallel paths implement the identical tree → bit-identical results across runs, machines, thread counts, and backends.
  - Rationale: "same everywhere, always" is the valuable property, not "same as the naive left fold"; pairwise error grows O(log n) vs O(n) for left-fold (more accurate AND faster); it unlocks the SIMD float-reduction speedup currently sacrificed on purpose (`lambda-vector.cpp:3442` — "float sum/prod left-fold stays scalar, no reassociation"); and it honors the MathLive float-order lesson correctly — define the order once, freeze it in spec.
  - Prior art: **NumPy** (`np.sum` = pairwise, 128-block, n-only), **Julia** (`Base.sum` pairwise), **Intel MKL CNR** (bitwise-identical across thread counts as a product feature), **NVIDIA CUB** (run-to-run-deterministic device reductions), **IEEE 754-2019** (recommends reproducible reductions) + **ReproBLAS**. Cautionary counter-examples of free-order: OpenMP `reduction`, Spark float `SUM`, TF atomics — all notorious reproducibility pain; PyTorch had to retrofit `use_deterministic_algorithms()`.
  - Boundary: user-supplied `reduce`/fold stays **strictly sequential** (purity ≠ associativity); decimal reductions use the same tree (decimal rounding is also non-associative). Accuracy escalations (Kahan/`fsum`-style, `method:'kahan'`) are **future explicit opt-ins, deferred until pressing** (precedent: `math.fsum`, KahanSummation.jl — opt-in everywhere).
  - Sequencing: semantics-ledger spec entry → scalar+SIMD pairwise implementation (collects the deferred SIMD win immediately, one-time golden migration) → Stage A parallel backend reuses the identical tree.
- **O1/O2 — Task scoping & cancellation: DECIDED (K30, 2026-07-08).** No new construct for either — two existing designs compose:
  - **K30a — Scoping via the resource-cleanup ledger.** A task handle is a **scoped resource whose release is cancel-and-join**: block-scoped (R2's rule, same loop-safety reason); **ownership escapes by return, visible in the return type** (R3) — long-lived services are handles resting with long-lived owners; escape-by-return *is* the escape hatch (no `GlobalScope` needed). Rejected: unstructured Go/Node lifetimes (the §6 fire-and-forget liability — orphaned tasks are concurrency's resource leak) and a dedicated nursery/scope construct (right semantics — Trio/Kotlin/Swift/Loom — wrong cost: new syntax plus an escape-hatch zoo).
  - **K30b — Exit semantics.** Normal block exit → **join** children (await completion); error exit (incl. `^`-propagation and cancellation from above) → **cancel children, then join**. Trio/Loom semantics, derived from the R-ledger's normal-vs-error asymmetry (the errdefer-for-free logic applied to tasks). K26's pipeline early-exit needs no special rule: the executor owns its scope and cancels explicitly when the terminal is satisfied.
  - **K30c — Cancellation is an error value at park points.** `cancel(h)` sets the task's flag and **unparks any pending suspension with a `T^E` `'cancelled'` error**; unwinding is ordinary `^`-propagation; auto-close cleanup runs on the error path (the cleanup design's "cancellation safety for free" promise, kept; R5-R: no `defer` keyword initially — auto-close is the cleanup mechanism); completion = cancelled `T^E`, ordered after prior sends (K20e), observable via `wait(h)`. No special exception type (Kotlin needed `CancellationException` with special rethrow rules; Lambda needs nothing), no token plumbing (.NET `CancellationToken`/Go `context` = coloring by another name), no kills. Documented v1 limit: CPU-bound stretches without park points don't observe cancellation (loop-back-edge safepoints later; a `check_cancel()` builtin as cheap interim if demanded).
  - **K30d — Cleanup masking.** Cleanup triggered *by* cancellation (auto-close; a future `defer` if ever added) runs with further cancellation masked for its duration (Trio's shielding lesson — otherwise cleanup that awaits is insta-cancelled and resources leak). Cleanup is short by convention; bounded risk.
  - **K30e — Authority & idempotency.** Any handle holder may `cancel(h)` (capability model, consistent with handle-as-address); idempotent; no-op on completed tasks.
  - **K30f — `wait(h, timeout:)`** times out the *waiter* with a `T^E` timeout error — it does **not** cancel the task (observing ≠ owning); cancel-on-timeout composes explicitly (`cancel(h)` after a timed-out wait, or a library wrapper).
  - `cancel` becomes the **seventh builtin** — still zero new keywords. Claim recorded: among Trio/Kotlin/Swift/Loom/.NET/Go, this is the only design where **neither scoping nor cancellation added a language construct** — scoping reuses R2/R3, cancellation reuses `T^E`.
- Carried forward: O4 (`select` syntax), O6 (handle typing vs C4 — handles are identities, the ref-cell family; also covers `self()` and handle-in-message semantics per K20a), O7 (grammar: `start` as contextual keyword), O10 (loop ordering spec). O9 becomes pure inference spec (seeds = builtins + module declarations; no user `async`). O1/O2 **resolved by K30** (above) — K26's streams prerequisite is satisfied at the design level. O3 **resolved by K20d** (bounded + `send → ok^E`). O5 (supervision) is **closed for v1** by K18 — handle-`T^E` is the failure surface; supervision is userland. **With K19, K20, and K30, the v3 design has no undecided design questions — only the carried spec-detail items above.**

### 10.9 v3 decision ledger + disposition of K1–K10

**New decisions (v3):**

- **K11** — Two-tier model: concurrent `pn` tasks + simple OS child processes; the worker-thread tier is dropped as a user concept; internal parallel-`fn` is the invisible third tier. *(user)*
- **K12** — Surface: `start` is the **only** concurrency keyword (contextual, `pn`-only); `wait`/`send`/`receive`/`select`/`process` are builtins; **no `async`, no `await` keyword**; direct calls stay synchronous/colorless. Naming rationale recorded in §10.2. *(user: `start` over run/fire/spawn/go; `wait` over `await`)*
- **K13** — **The capture rule**: `start` closures must not capture `var`s by reference (compile error); communication only via messages + read-only flats. Makes task semantics thread-agnostic — ships with `start` in v1, unretrofittable. *(proposed in discussion, unchallenged — treat as adopted; needs grammar/analyzer spec)*
- **K14** — Mechanism: K2-R state machines confirmed for phase 1 *because of* thread-portability (Kotlin/C#/tokio M:N precedent); fibers (§4.2.4) remain the documented fallback. *(user: "fine with state machines if it goes well with threading")*
- **K15** — Multicore is committed: **Stage A** fork-join parallel-`fn` (arenas + GC blackout + runtime-globals audit; internal, invisible) → **Stage B** M:N `pn` tasks (thread-safe runtime); BEAM-style M:N isolates recorded as the Stage-B alternative. *(user: "run pn on thread is 100% what I want"; fn too, internal)*
- **K16** — `async` removed from user grammar; async-ness declared only in native-module signatures; **JS membrane: all exposed `pn`s uniformly Promise-returning** (opt-in `sync` annotation possible later). *(user)*
- **K17** — JS implementation unification per §10.7: share layers 1/2/5, keep 3/4 per-language; Lambda transform built first, common core extracted after two working clients. *(user)*
- **K18** — Processes are simple OS child processes: no supervision trees, links, green processes, or distribution in v1; handle-as-`T^E` is the entire failure surface. BEAM-likeness caveat recorded. *(user: "simple child process, not BEAM's kind")*
- **K19** — **Pairwise-by-spec numeric reductions** (closes O-B): builtin reductions defined as a fixed n-only tree order, identical across scalar/SIMD/parallel backends → bit-identical everywhere; user folds stay sequential; Kahan-style accuracy modes deferred to explicit opt-in. Full rationale + prior art (NumPy/Julia/MKL CNR/CUB/IEEE 754-2019) in §10.8. *(user: "Option 2 should definitely be the policy")*
- **K20** — **Mailbox messaging, actor-model** (closes O-A): handle = address, no channel type (K20a); N:1 by design, pools via explicit dispatcher (K20b); heterogeneous messages with **FIFO-head receive — no in-queue selective receive**, making BEAM's scan trap unrepresentable (K20c); **async bounded send returning `ok^E`** — backpressure as an error value, blocking variants (`wait(send(...))` / `sync:` option) deferred to explicit opt-in (K20d); **signal-ordering guarantee** — sent messages outlive the sender, termination observable only after all prior sends, end-of-stream = handle completion (K20e). Full reasoning + actor-vs-CSP comparison in §10.8. *(user decision + two accepted refinements)*
- **K30** — **Task scoping & cancellation** (closes O1/O2): handles are **scoped resources whose release is cancel-and-join** (R2 block scope, R3 escape-by-typed-return — no nursery construct, no `GlobalScope`); normal exit **joins**, error exit **cancels-then-joins** (Trio/Loom semantics from the R-ledger asymmetry); cancellation delivered as a **`T^E` `'cancelled'` error at park points** via `cancel(h)` (the seventh builtin — no exception type, no token plumbing); cleanup runs cancellation-masked; any holder may cancel, idempotently; `wait` timeouts don't cancel. Details + sub-decisions K30a–f in §10.8. *(user-confirmed)*

**Disposition of the v1/v2 ledger:**

| Entry | v3 status |
|---|---|
| K1 (`fn` never suspends; suspension is `pn`-only) | **kept** — unchanged foundation |
| K2 (fibers) | superseded (was already superseded by K2-R); reference design §4.2.4 |
| K2-R (state machines) | **kept as mechanism** (K14); its `async`-declaration surface removed by K16 |
| K3 (one loop per context, shared with JS) | **kept** |
| K4 (unified isolate API, isolation as option) | revised → K11: two tiers, `start` operand picks the tier; uniform handles survive |
| K5 (copy messages; share flat immutables; Mark wire) | **kept** |
| K6 (failures are values; handles → `T^E`) | **kept** — now the *entire* failure model (K18) |
| K7 (no migration; 1 thread/isolate) | revised: phase-1 true; "never" dropped — Stage B schedules tasks M:N (K15); semantics made migration-safe by K13 |
| K8 (channels excluded; mailbox + select) | **REOPENED** → O-A; `select` and boundedness survive regardless |
| K9 (level 0 par-map) | revised → internal parallel-`fn`, Stage A of K15; determinism principle kept, float caveat added (O-B) |
| K10 (libuv shared reactor; JS async untouched; membrane rule) | **kept**; extended by K17 (shared transform layers) and K16 (uniform-Promise membrane) |

### 10.10 Dependencies & sequencing (v3)

1. **G1 does not gate tasks** (state machines — heap objects). G1 remains the top runtime-honesty item independently; Stage A needs only the GC-blackout discipline, Stage B is where GC work returns at full scale.
2. **Ships together or never:** `start` + the capture rule (K13) + bounded delivery + **K30 scoping** (block-exit join/cancel is observable behavior of `start` — adding it later changes program meaning). The unretrofittable set.
3. **JubeHostAPI v1 clauses unchanged** (§4.4 blocking/await-safety declaration + G2 rooting) — plus the async-ness marker in module signatures (K16).
4. Build order: **tasks** (compiler track: may-await inference + shared-pattern state-machine lowering in `transpile-mir`; runtime track: scheduler + uv integration + handles/`wait`) → **processes** (`process()` spec, Mark-over-pipe, shm flats — mostly OS plumbing) → **Stage A** parallel-`fn` (arena workers + globals audit; unlocks the typed-array/image workloads) → **Stage B** when demand proves it (semantics already ready).
5. O-A is decided (K20 — mailbox): the `send`/`receive`/`select` builtins can freeze against K20a–e. O-B is decided (K19); its spec entry + scalar/SIMD pairwise implementation can proceed **independently of and before any concurrency work** — it collects the deferred SIMD float-reduction win today and pre-stabilizes goldens for Stage A.
6. **Radiant is the first embedder of this design**: pages as isolates, events as mailbox messages, display lists as flat/K29 payloads, same-thread script+layout — see `radiant/Radiant_Design_Concurrency.md` (ledger RC1–RC8).
7. **Implementation plan**: `Lambda_Concurrency_Impl_Plan.md` — phase-gated plan for the v1 scope (colorless `pn` + `start`/messaging + JS interop); processes, threading, streams, and cancellation are queued follow-ons there.

---

## 11. Streams × concurrency (added 2026-07-08, ledger K21–K26)

How the v3 concurrency model carries the lazy-stream design (`Lambda_Design_Data_Processing.md` §8, D9–D12; its §8.6 cross-references back here). Conclusion first: **the pieces compose with almost no friction, because each side was designed with properties the other needs** — and the five genuinely new decisions are small and recorded below.

*Follow-on (2026-07-14): `Lambda_Design_Pipeline.md` (ledger PL1–PL11) extends this section to three pipeline kinds — text (a framing preset over the data pipeline), data (this design, unchanged), and binary (re-chunking license, flat sub-binaries, byte-metric queues, transducer stages, raw-byte process spawn) — with WHATWG byte-stream conformance and the Node shim split per K27/K28.*

### 11.1 Why the pieces click — four enablers and one convergence

1. **Laziness supplies the plan** (D9/D10): a stream is a *recorded pipeline*, not running code — the executor is free to choose sequential or concurrent execution at forcing time, invisibly.
2. **The `fn`/`pn` split pre-computes what's safe**: `fn` stages are parallelizable/fusible by *verified* purity (the optimizer's license, D11); `pn` stages are ordered barriers. Pipeline segmentation into parallel-safe and order-anchored sections falls out of the type system.
3. **K13 + value semantics make items transferable**: an Item crossing between stage tasks has no shared-mutable-state hazard, by construction.
4. **K19 keeps parallel results deterministic** at the terminals (reductions).

**The convergence:** K20e — "receivers get all messages, per-sender FIFO, then termination carrying the final `T^E`" — is *exactly* D12's stream requirement ("all the messages + a proper end-of-stream signal"), decided independently in the mailbox discussion. The messaging contract and the stream contract are the same contract. That identity is what makes §11.2 an adapter rather than a subsystem.

### 11.2 Mailboxes pipe: handles as stream sources and sinks (K21)

A task or process handle is a natural **stream source**: its messages are the elements; K20e handle-completion is the end-of-stream; a failed task surfaces as `T^E` at the forcing point (D12's error path, unified):

```lambda
pn main() {
    let p = start process("scraper.ls", urls)     // child process streams results
    stream(p) |> where(.status == 'ok') |> group(.domain) |> output("report.json")
}                                                  // end-of-stream = p's handle completing
```

- **Source:** `stream(h)` — lazy stream of the handle's messages until completion. In the D10 taxonomy this is cleanly a **live-I/O stream** (one-shot, `pn`-only); no new category.
- **Sink:** a `send_to(h)` terminal forwards each result as a message — making any pipeline a producer for another task.
- **Cross-process for free:** messages already ride Mark over the pipe (K5), so `start process(...)` + `stream(p)` composes distributed pipeline segments with zero extra machinery.
- **Push-pull reconciliation:** file-backed `stream()` sources are pull; mailbox sources are push-fed — and the **bounded mailbox is itself the reconciling buffer**: the producer runs ahead only to capacity, the consumer pulls at its pace, backpressure emerges from boundedness (K20d). No new mechanism.

### 11.3 Execution at forcing time: what parallelizes, and how order survives

- **K22 — Ordered by default.** Streams are ordered sequences; a parallelized `fn` stage must not reorder its output. Workers process chunks; a re-sequencing buffer emits in index order — deterministic, K19's sibling policy (same philosophy: fixed order as spec, parallelism invisible). An `unordered` opt-in may come later for throughput; deferred until pressing.
- **K23 — `fn` segments auto-parallelize; `pn` stages are sequential anchors.** Stage-A fork-join applies invisibly to `fn` segments only. Auto-pipelining *across* `pn` stages is excluded in v1: it would change effect interleaving relative to sequential forcing (stage 1 on item 2 concurrent with stage 2 on item 1) — `pn`-confined nondeterminism, arguably legal, but not to be sprung on users silently. Users who want pipeline parallelism across effectful stages build it **explicitly** with `start` + mailbox streams (§11.2) — explicit is the right default for effects.
- **K24 — The executor uses a runtime-internal blocking send.** User-level `send` stays async-`ok^E` (K20d unchanged); but runtime-generated stage plumbing cannot meaningfully "handle" a full-mailbox error — it parks until space. The deferred blocking variant thus found its pressing need early, **as an internal primitive only**; the user surface is untouched.
- **Granularity (internal note):** message-per-element is too fine for internal plumbing; the executor batches chunks between stage tasks — invisible under K22's ordering guarantee. User-visible mailbox streams stay message-grained (the user controls granularity by what they send).

### 11.4 Lifecycle: resources and cancellation across tasks

- **K25 — Passing a resource into `start` is an ownership escape** (extends R3 of the resource-cleanup ledger, `Lambda_Semantics_Features.md` §3.5). A live source consumed by a spawned stage task is used outside the block that acquired it: the task takes ownership, auto-close moves to the task's end, and a cancelled or failed task runs its cleanup (cancellation is an error-shaped exit, so R2's auto-close fires — already designed). One-clause addition to the escape rules.
- **K26 — A forced pipeline is an implicit task scope.** Early termination (`first(10)` on a concurrent pipeline) must *cancel* upstream tasks and close sources — backpressure alone never terminates an infinite source. Scope exit (normal or error) cancels stage tasks; cancellation runs their cleanup. **Consequence: O1/O2 (task scopes + cancellation) are promoted from carried detail to prerequisite for concurrent stream execution** — *now satisfied at the design level by K30 (§10.8): the executor owns its scope and cancels on terminal satisfaction, per K30b's owner's-prerogative rule.*

### 11.5 Cross-reference map

| This section | Composes with |
|---|---|
| K21 mailbox streams | K20e (the contract identity) · D10 taxonomy (live-I/O kind) · D12 (`on error` + `T^E` at forcing) · K5/K18 (Mark over pipe) |
| K22 ordering | K19 (fixed-order philosophy) · D11 (`fn` fusion license) |
| K23 fn-only auto-parallel | K15 Stage A (the machinery) · K1 (`fn` never suspends) · governing principle 3 (nondeterminism confined to `pn`) |
| K24 internal blocking send | K20d (user surface unchanged) |
| K25 resource escape | R3/R2 (`Lambda_Semantics_Features.md` §3.5) · D12 (stream source release on failure) |
| K26 pipeline scope | O1/O2 (now prerequisites) · R-ledger cleanup adjacency · O2 cancellation semantics |

Open after this section: nothing new — K21–K26 resolve into existing carried items (O1/O2 gain urgency; O4/O6/O7/O10 unchanged).

### 11.6 Streams across languages — the comparison, and the shared core with Node (K27)

#### 11.6.1 The streaming-language landscape

| | Model | Payload | Pipeline is a… | Backpressure | Parallelism | Errors | Early termination |
|---|---|---|---|---|---|---|---|
| **Lambda (D9–12, K21–26)** | pull, plan-based | typed Items | **re-forcible value** (plan) | bounded mailboxes/queues | `fn` segments auto (Stage A); explicit tasks/processes | `T^E` at forcing + `on error` | pipeline = task scope (K26) |
| **Node streams (legacy)** | push w/ pull accommodation, event-driven | bytes first, objectMode bolted on | live wired objects | `highWaterMark` + `write()→false` + `'drain'` | none | `'error'` events; un-propagated through `pipe()` (→ `pipeline()` retrofit) | `destroy()`, premature-close footguns |
| **WHATWG Web Streams** | **pull, promise-based, credit (`desiredSize`)** | any JS value | wired objects, locked | built-in queuing strategies | none | promise rejection | `cancel()` / `AbortSignal` |
| **Nushell** | pull (Rust iterators) | structured values | live iterators | implicit via pull | externals = OS processes | `LabeledError` | drop the iterator |
| **Bash/Unix** | push into kernel buffer | bytes/text | live processes | **kernel pipe buffer — bounded, blocking** | **yes — process per stage** (the original pipeline parallelism) | exit codes; `pipefail` off by default | **SIGPIPE** |
| **jq** | pull, generator semantics (0..N outputs per input, backtracking) | JSON values | filter expression | n/a (generators) | none | abort | `first`/`limit` short-circuit |
| **Java Streams** | pull, plan-based | objects | lazy source→ops→terminal | n/a (pull) | `.parallel()`, encounter order kept | exceptions | short-circuit terminals |
| **GenStage / Akka Streams** | **demand-driven (credit) actor stages** | messages | **blueprint, materialized later** (Akka) | demand protocol | yes — stage per actor | supervision / materialized failure | cancellation flows upstream |

Lessons absorbed (and where):
- **Unix** got two things right in 1973 that successors lost: the bounded kernel buffer (backpressure by blocking = K24) and **SIGPIPE** (early termination propagating upstream — K26 is its structured descendant). Its failures — untyped bytes, `pipefail` defaulting off — are Lambda's payload and error models inverted.
- **jq**: the practical subset of its everything-is-a-generator semantics is covered by for-comprehensions; its `--stream` mode carries an implementation note for P8 — **`stream("big.json")` requires the parser itself to be incremental**, not just the pipeline.
- **Nushell**: the typed-pipes cousin, but its pipeline is live iterators — no plan value, no re-forcing, no optimization.
- **Java Streams**: the closest *API-shape* precedent (lazy source → fusible ops → forcing terminal; parallel opt-in; encounter order kept) — mainstream validation of the D9/K22 shape. Lambda is stronger where it counts: Java's parallel reduce *trusts* combiner associativity (silently varying results if wrong); Lambda **verifies** purity and **pins** order (K19/K22). No row in the table offers deterministic-under-parallelism as a guarantee; that cell is Lambda's alone.
- **GenStage/Akka Streams**: the closest *architecture* precedent to §11 — demand-driven stages as actors ≅ mailbox streams; Akka's blueprint/materialization split ≅ plan-value/forcing (their "materialized value" ≅ the handle). Industrial-scale proof of the K21 shape.

**Lambda's distinctive combination** (no single row has all four): pipeline-as-value (re-forcible, optimizable) + *verified*-pure fusion and parallelism + deterministic-by-spec results + errors as `T^E` values with ordered end-of-stream. The accepted trade: no push-based hot sources at the surface — the bounded mailbox absorbs push at the edge (K21), the right call for a data language.

#### 11.6.2 K27 — the shared stream core with Node/WHATWG

Status quo: `js_stream.cpp` (~10k lines) already implements Node's legacy stream classes (Readable/Writable/Duplex/Transform/PassThrough + `pipeline()`) over EventEmitter with a simplified push/pull model — working but incomplete; **streams remain the node-baseline linchpin**. Meanwhile Node itself is migrating to **WHATWG Web Streams** (pull, promise-based, credit via `desiredSize`, `AbortSignal`), shipping `toWeb`/`fromWeb` adapters. That migration is the unification gift: **WHATWG semantics are nearly isomorphic to Lambda's** (pull + bounded queue + promised next + cancel ≅ stream value + mailbox + `wait` + scope-cancel).

**K27 — four-layer shared core** *(user-confirmed)*:

- **Layer 0 — uv sources/sinks, one implementation.** K10's "shared uv services, two thin fronts" extends to streams verbatim: fs/net/http/stdio sources built once; `stream("file")` and `fs.createReadStream` are two faces of one uv reader.
- **Layer 1 — one C-level primitive: the bounded Item chunk-queue with completion.** Bounded queue of Items + completion state carrying `T^E` + cancel signal. This single object implements: Lambda's inter-stage executor queues (K24), the task mailbox backing `stream(h)` (K21), Node's `_readableState` buffer (`highWaterMark` = the bound), and WHATWG's internal queue (`desiredSize` = remaining capacity). The contracts already align three ways:

  | Lambda (K20e) | Node events | WHATWG |
  |---|---|---|
  | message | `'data'` | `{value, done: false}` |
  | handle completes, ok | `'end'` | `{done: true}` |
  | handle completes, `T^E` | `'error'` | promise rejection |
  | scope cancel (K26) | `destroy()` | `cancel()` / abort |

  The payload unification is the part **only Jube can do**: Node Buffers are typed arrays are Items; objectMode values are Items — the queue element is uniformly `Item`, so cross-language streaming is queue-sharing, never serialization.
- **Layer 2 — surfaces stay separate; legacy Node is a shim.** Lambda stream values keep plan semantics; **Web Streams** get a thin spec-faithful face on the core; **legacy Node streams (`js_stream.cpp`) re-base as a compat shim over the Web-Streams-shaped core** — architecturally what modern Node itself is. Legacy stream *observable semantics* (event timing, flowing/paused switching) are NOT unified — dialect-faithful shim territory, regression-gated by the node baseline. K17's protect-what's-green sequencing applies verbatim: build the core under the Lambda executor first or alongside; re-base `js_stream.cpp` only against a green node baseline.
- **Layer 3 — cross-language adapters become trivial**: `stream(nodeReadable)` (live-I/O source), `toReadable(stream_or_handle)`, and the Web-Streams pair — each a few dozen lines over the shared queue, because Layer 1 made the contracts identical.

Payoff, mirroring K17's logic: the node-baseline streams linchpin and the P8 stream executor are currently two future efforts; K27 makes them **one core plus faces** — the node suite as the shim's harness, the Rosetta/stream suites as Lambda's.

**K28 — the compat commitment split** *(user-confirmed)*: **WHATWG Web Streams are what Lambda commits to** — designed-for, first-class, **aiming 100% spec conformance**; the core (Layer 1) is shaped for WHATWG semantics by construction. **Legacy Node streams are best-effort only** — supported through the shim, prioritized by what real packages need, with **no 100%-pass promise**: the legacy API's three generational rewrites, mode-switching edge cases, and event-timing folklore are exactly the compat treadmill J5 exists to refuse. This is the first concrete instance of the per-guest supported-subset declarations that gap G8 (`Lambda_Semantics_Features.md` §1.8) calls for — and it bets with Node's own direction of travel (Web Streams are the standard; legacy streams are Node's past). Practical consequence for the node baseline: legacy-stream failures are triaged as "shim gap — fix if a real package needs it," not as conformance bugs.

### 11.7 Message wire format (K29)

Where a wire exists and what runs on it — decided 2026-07-08 *(user-confirmed)*:

- **K29a — Task ↔ task (in-context): no serialization, ever.** Items copy/COW heap-to-heap; big immutable flats share by refcount. Most messaging never touches a wire — this is the performance story, and it is settled.
- **K29b — Flats across processes: serialized, not pointer-shared (v1).** Pointers never cross a process boundary — that is exactly why sharing was restricted to flat *pointer-free* values (K5): a shared buffer contains only raw element data; pointers exist only in each process's local header. **v1 ships the simple mechanism: a flat's raw buffer is serialized inline into the message** (in binary form: tag + length + `memcpy` — no text encoding of numbers). The zero-copy alternative — a small serialized *descriptor* (shm segment handle + offset + shape), each process mapping the segment locally — is a **deferred optimization** for very large buffers, with BEAM's inline-vs-refc binary threshold (64 bytes) as the precedent for a size-based split when the need arrives.
- **K29c — Process ↔ process: two encodings of one model, text now, binary later.** Mark remains the single model. **Text Mark** is the working format today and remains first-class forever as the debug/log form (a pipe can run in text; any message is loggable via the existing formatter — you can `cat` the conversation). **Binary Mark** is the eventual production wire: self-describing, schemaless, covering every Item type by construction (elements, symbols, decimal128, datetime, N-D typed arrays as raw buffers), length-prefix framed so the same encoding carries K27 stream chunks. Its detailed encoding design is **explicitly deferred — no further work now**; when designed, it will likely borrow wire techniques from Protobuf and MessagePack (varints, tag-length-value, ext-type conventions) while rejecting what disqualified them as the wire itself: Protobuf's schema-first model and MessagePack's Item-model impedance. Both remain candidates as *conversion formats* (`format(x, 'msgpack')`) — never the IPC wire.
- Non-goals restated: binary Mark is a streaming encode/decode, **not** the position-independent frozen-arena graph format (deferred indefinitely, K5), and it is value-shaped — columnar interchange stays Arrow's job in the deferred I/O proposal. Three formats, three jobs.

---

## 12. Phase-1 implementation specifications (pinned 2026-07-15)

This section closes O6/O7/O9/O10 for the in-context task tier implemented by
`Lambda_Impl_Concurrency.md`. It is normative for the Phase-1 implementation;
the process, streams, and parallel-`fn` tiers remain outside that plan.

### 12.1 O10 loop ordering

There is one libuv loop per Lambda context. A loop turn observes this order:

1. finish the currently executing Lambda task or host callback;
2. drain JS `process.nextTick` and Promise microtasks to quiescence;
3. append Lambda resumes made ready during that job to the Lambda FIFO run queue;
4. run ready libuv callbacks for the turn;
5. drain the JS microtasks created by those callbacks;
6. run Lambda tasks from the ready queue in FIFO readiness order.

A Lambda resume is therefore a macrotask: it never interrupts a JS job or its
microtask checkpoint. A task's successful `send` operations enqueue into the
receiver mailbox before the sender completion is published. Send completion is
the return of that synchronous enqueue attempt; handle completion is queued only
after all sends performed by that task have returned. Multiple task resumes made
ready by one callback retain registration order.

### 12.2 O9 may-await closure

The seed set is every `pn` containing a call to a registry entry whose
`is_async` flag is set (`wait`, `receive`, `select`, `sleep`, and the v1 async
`io.read` operation), plus native-module procedures declared async. The compiler
then computes a fixed point over static `pn` call edges. A `pn` that calls a seed
or another may-await `pn` is may-await. A call through a `pn`-typed value,
closure, higher-order parameter, or otherwise unresolved procedural callee is
conservatively may-await. `fn` values never enter the closure.

Each `AstFuncNode` records the bit and its first causal edge. Diagnostics use:
`procedure '<name>' may suspend because it calls '<callee>'`, with `indirect pn
call` as the callee for conservative edges. The MIR transform and JS export
membrane consume the same bit; no second inference is permitted.

### 12.3 O6 handle representation

The v1 task handle is an opaque, immutable, branded VMap. The VMap's native
payload points to the scheduler-owned task record; its brand distinguishes task
handles from ordinary maps and other host objects. The scheduler, not the VMap
finalizer, owns the task record because completion, waiters, and mailbox delivery
may outlive any one handle reference. Scheduler teardown releases task records
after unregistering their GC roots.

Handles compare by VMap pointer identity. Their fields and backing store are not
script-mutable; the only operations are the concurrency builtins. Handles may be
sent in in-context messages. This representation deliberately avoids a new
`TypeId`; a first-class static `handle` type remains a follow-on O6 typing task.

### 12.4 Constants, delivery errors, and script exit

- Default mailbox capacity: **1024 Items**.
- A full mailbox makes `send` return an error value with code
  **`'mailbox_full'`**; it never blocks and never drops silently.
- Cancellation and timeout use **`'cancelled'`** and **`'timeout'`**.
- Normal CLI execution drains the shared loop until no live Lambda task remains.
  `--no-drain` disables that final drain for harnesses. The drain is bounded by
  the existing event-loop watchdog policy; exhaustion is reported, never hidden.

### 12.5 O7 contextual `start` grammar

`start` is contextual only where an expression begins. The grammar node is
`start_expr: 'start' call_expr`; its operand must resolve to a `pn` call. Existing
bindings, parameters, fields, and map keys named `start` remain identifiers.
`start` is legal only inside a `pn`; no `async` or `await` syntax is added.

### 12.6 K30 scope, escape, and cancellation rules

A started handle is owned by the nearest lexical block. Returning that handle
transfers ownership to the caller. Storing or sending the handle exposes a
capability but does **not** transfer scope ownership. Every non-escaped child is
joined on normal block exit. Error exit, including `^` propagation and
cancellation from above, cancels each non-escaped child and then joins it.

Exit-edge lowering is a reusable compiler operation parameterized by normal or
error exit; concurrency is its first client and auto-close may reuse it later.
Cancellation sets an idempotent flag and unparks a parked task with
`T^E 'cancelled'`. Park entry checks a pre-existing flag. Cleanup reached because
of cancellation runs with further cancellation masked until the cleanup join
finishes. `wait(h, timeout:)` returns `T^E 'timeout'` to the waiter only and does
not cancel `h`.

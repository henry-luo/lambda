# Radiant Concurrency Design — Pages as Isolates, Display Lists as Values

**Status:** seed design — direction confirmed 2026-07-08 (ledger RC1–RC8; open items RO1–RO8)
**Context:** applies the Lambda concurrency design v3 (`../Lambda_Design_Concurrency.md`, ledger K11–K29) to Radiant, which today is single-threaded apart from network threads. The goal: carry Lambda's model — isolates, mailboxes, handles, values-as-payloads — into the engine, rather than inventing a second concurrency vocabulary for Radiant. Radiant is the first *embedder* of the v3 machinery: pages use the isolate topology (K7/§5.5) at the embedding level, while page *scripts* continue to see only the v3 language surface (`start`, `wait`, mailboxes).

---

## 1. Prior art — where browsers put their threads, and why

- **Chromium**: process per site; within a renderer, the **main thread runs JS + DOM + style + layout + paint-recording together**; a compositor thread owns scrolling/animation; rasterization on a tile-parallel pool; GPU in its own process. The cut is *below paint* — never between script and layout.
- **Servo** — the direct cautionary tale: it ran **script and layout as separate threads per page, communicating by messages**, for a decade. It worked, but every synchronous layout query from script became a blocking RPC round-trip, and the DOM had to be readable from two threads (COW snapshots). In the 2023 layout rewrite Servo **merged layout back onto the script thread**, keeping parallelism *inside* the layout pass (work-stealing) instead of *between* script and layout.
- **Firefox Quantum**: **Stylo** parallelized style resolution with a work-stealing pool (embarrassingly parallel per subtree); **WebRender** moved rasterization to dedicated threads; layout stayed on the main thread with JS.

**Convergent lesson: the profitable thread boundaries are (a) per-page isolation and (b) below layout (compositing/raster) — not script|layout.**

## 2. The synchronous-query trap — why script and layout share a thread (RC1)

JS (and DOM-scripting Lambda) has spec-mandated *synchronous* layout queries: `offsetWidth`, `getBoundingClientRect`, `getComputedStyle`, `scrollTop`. Script mutates the DOM, then asks a question whose answer requires flushing style + layout *mid-script* — and real pages do this in loops (the classic layout-thrash pattern). With layout on another thread, every query parks the script task for a message round-trip on the hottest path in the engine, and the DOM becomes cross-thread-shared mutable state.

Radiant has an additional, decisive constraint: **`DomNode` is simultaneously the DOM node and the layout view** (the dual nature in `radiant/`'s design) — script-visible state and layout state are one allocation. A script|layout thread split would require untangling that first, for a payoff the entire industry has walked away from.

**RC1 (decided):** per page, **script + style + layout + display-list build run on one thread**. Parallelism comes from per-page isolation, from fork-join *inside* the style/layout passes, and from the compositor split below — never from splitting script and layout.

## 3. Architecture

| Radiant unit | Lambda-v3 concept | Thread | Communicates via |
|---|---|---|---|
| **Shell** — windows, menus, OS input, page lifecycle | main context | main | holds page handles; `send`s input as messages |
| **Page** (one per page/tab) — JS + Lambda script, DOM, style, layout, display-list build | **isolate: a context on its own thread** (K7/§5.5 embedding topology — not the language-surface tier) | one per page | mailbox in (events); display lists out; death = `T^E` on handle |
| **Compositor/renderer** — scroll & animation transforms, raster (tile-parallel pool), GPU/window | dedicated task/thread | one per app (scalable per page, RO3) | receives **display lists as immutable values** |
| Style/layout internal phases | Stage-A-style fork-join pool (K15; Stylo/Servo precedent) | internal workers | fork-join, no messages |
| Network / resource loading | shared uv services (K10) | uv pool | completions delivered on page loops |
| Untrusted content (later) | K18 child process — same handles | own process | K29 wire; same architecture, one option flip |

### 3.1 The page isolate

- Owns its DOM, computed styles, view tree, and the K3 loop that multiplexes JS jobs, Lambda task resumes, timers, rAF, and incoming event messages. LambdaJS and Lambda scripts run here unchanged; the page loop *is* their shared K3/K10 loop.
- **Page scripts see only the v3 language surface**: `start` tasks ride the page loop (level 1 — ideal for I/O concurrency inside a page); `start process(...)` covers heavy compute today; Stage B (M:N tasks) is the eventual relief for CPU-bound page work. A CPU-bound stretch on the page thread janks *that page only* — same trade every browser makes.
- **Per-page heap for free (RC2):** each page is a Lambda context — own heap, own GC. Closing a tab = dropping a context (leak containment browsers had to fight for). A crashed page is a `T^E` on its handle at the shell: tab-crash UI, not app crash (K18's failure model doing browser work — RC7).

### 3.2 The compositor split and the display list (RC3)

After layout, Radiant already produces a paint IR / display list — an **immutable value describing a frame**. That value is the architectural hinge:

- **In-process today:** shared to the compositor as a refcounted flat (K5) — zero-copy handoff.
- **Cross-process later:** serialized over the K29 wire — pages-as-processes (site isolation) becomes an option flip, not a redesign.
- **Testing bonus:** a Mark-serializable display list enables golden-file testing of paint output and record-replay debugging — worth building the serialization even before any process split (RO8).

The compositor owns scroll and animation transforms, so scrolling stays live while a page thread is busy — contingent on RC5.

### 3.3 Events as messages (RC4)

OS input arrives at the shell and is routed as Item messages to the page isolate's mailbox (K20). **K20e's per-sender FIFO ordering *is* input-order preservation.** The existing `event.cpp` dispatch logic runs unchanged inside the page thread. Timers and rAF live on the page's own loop; vsync ticks arrive as messages from the compositor.

**RC5 — passive-by-default listeners:** the compositor may scroll without consulting the page thread unless a listener registered as non-passive exists on the target chain (the modern web default). Adopted from day one so smooth scrolling survives page-thread jank; non-passive listeners force the synchronous route per-target.

### 3.4 Internal parallelism (RC6)

Style resolution is embarrassingly parallel per subtree (Stylo); layout has parallelizable phases (Servo's work-stealing precedent; intrinsic sizing per subtree). Both run on a fork-join worker pool *inside* the page's frame production. Two notes:

- This pool works on **non-GC C++ data** (pool-allocated views, computed styles) — simpler than Lambda's Stage A: no arenas, no GC blackout. It can be built independently of the Lambda-side Stage A machinery, sharing only the thread-pool substrate.
- **Determinism is non-negotiable:** parallel style/layout must produce results identical to the sequential engine — fixed merge order, parallelism invisible (the K22/K19 philosophy applied to C++ passes). `make layout` goldens and the UI-automation baseline (5714) are the regression harness.

## 4. Decision ledger (RC)

- **RC1** — Same-thread script + style + layout + display-list build per page; no script|layout thread split (Servo's lesson; the sync-query trap; `DomNode` dual nature). *(user-confirmed)*
- **RC2** — Page = isolate: one Lambda context on one thread per page; per-page heap/GC; page teardown = context drop. Embedding-level use of K7/§5.5 topology — the language surface is unchanged.
- **RC3** — The cut is below layout: compositor/renderer thread receives **display lists as immutable values** (refcounted flat in-process, K29 wire cross-process later).
- **RC4** — Events are mailbox messages, shell → page; K20e ordering = input order; `event.cpp` dispatch unchanged within the page.
- **RC5** — Passive-by-default event listeners, so compositor scrolling never blocks on the page thread by default.
- **RC6** — Style/layout internal parallelism is deterministic fork-join (fixed merge order); layout goldens + UI-automation baseline gate it.
- **RC7** — Page crash containment: page death/crash = `T^E` on its handle at the shell (tab-crash UI, not app crash).
- **RC8** — Network threads fold into the shared uv services (K10), completions delivered to page loops.

## 5. Open items (RO)

- **RO1 — The page-thread scheduler.** The page loop multiplexes input messages (highest priority) > rAF/frame production > JS jobs + Lambda resumes (O10 ordering) > idle work. Blink has an entire "renderer scheduler" discipline; Radiant needs its version — this is O10 growing a Radiant chapter, and it is genuinely new design work.
- **RO2 — Flush-point spec.** Style/layout flushes interleave with script at defined points only: flush-on-query (the sync APIs), flush-on-frame — never mid-job otherwise. Dirty-flag bookkeeping specified against the Lambda/JS loop explicitly.
- **RO3 — Compositor granularity**: one compositor thread per app vs per page/window — decide by measurement.
- **RO4 — vsync/rAF plumbing**: tick source, delivery as messages, frame-budget accounting.
- **RO5 — Hit-testing split**: compositor-side fast path for scroll targeting vs full hit-test on the page thread (needs layout data — Chromium's split as reference).
- **RO6 — Multi-window shells**: one shell context + N windows assumed; confirm against the application-shell design (RAD docs).
- **RO7 — IME / focus / editing**: synchronous-feeling interactions (composition events, caret) stay page-thread-local; verify against the editing/interaction-state designs.
- **RO8 — Display-list serialization**: build the Mark form early for golden testing; revisit shared font/image caches when pages become processes.

## 6. Sequencing sketch

1. **Display list → compositor thread** (RC3): the biggest single UX win (smooth scroll/animation independent of page jank) and buildable *before* page isolates exist — today's single page becomes "one page thread + compositor."
2. **Page isolates** (RC2): requires the Lambda context-per-thread embedding infra (the §5.5 topology); multi-page/tab arrives here, with RC4 event routing and RC7 crash containment.
3. **Parallel style pool** (RC6, Stylo-shaped) — measurable wins on large documents; goldens gate.
4. **Parallel layout phases** — after the style pool proves the harness.
5. Pages-as-processes (site isolation) — deferred until untrusted content matters; the architecture is already shaped for it (RC3 + K29).

## Cross-references

| This doc | Lambda concurrency design (`../Lambda_Design_Concurrency.md`) |
|---|---|
| Page isolate (RC2) | K7 topology, §5.5 ("main context owns Radiant/UI"), K11 two tiers |
| Events as messages (RC4) | K20 mailboxes, K20e ordering |
| Display-list payloads (RC3) | K5 flat sharing, K29 wire format |
| Crash containment (RC7) | K18 handle-as-`T^E` failure model |
| Fork-join passes (RC6) | K15 Stage A (thread-pool substrate), K22/K19 determinism philosophy |
| Page scripts' concurrency | K12 surface (`start`/`wait`), K3/K10 loop, §4.6 JS coexistence |
| Scheduler (RO1) | O10 loop-ordering spec |

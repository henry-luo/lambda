# Lambda Concurrency — Implementation Plan (v3, Phase 1 Scope)

**Status:** implementation plan — derived from the confirmed design in `Lambda_Design_Concurrency.md` (v3, ledger K11–K29)
**Date:** 2026-07-08
**Scope (user-set):** (1) colorless concurrent `pn` under Lambda; (2) `start` + `send`/`wait`/`receive`/`select` messaging; (3) interop and unification with JS async.
**Explicitly OUT of scope for this plan:** the process tier (`start process(...)`, K18/K29 wire), Stage A/B threading (K15), streams integration (K21–K28, P8), supervision, binary Mark. Each is a follow-on with its design already recorded.
**Pulled INTO scope (2026-07-08): task scoping & cancellation (K30).** Block-exit join/cancel semantics are observable behavior of `start` — shipping `start` unscoped and adding K30 later would change program meaning. Like K13, it ships with `start` or the behavior breaks later: the unretrofittable set is now `start` + K13 capture rule + bounded delivery + **K30 scoping**.

**Design anchors:** K1 (`fn` never suspends) · K2-R (may-await state machines, Phase-6 family) · K12 (`start` keyword + `wait`/`send`/`receive`/`select`/`self` builtins; no `async`/`await` keywords) · K13 (capture rule) · K14 (state machines because of thread-portability) · K16 (no `async` in grammar; uniform-Promise membrane) · K17 (build Lambda transform first; extract shared core after two clients) · K20a–e (mailbox semantics) · **K30a–f (scoped tasks, cancellation as `T^E` at park points, `cancel` as seventh builtin)** · K3/K10 (one libuv loop shared with JS) · O9/O10 (the two specs this plan must write before coding).

---

## Phase 0 — Specs and pinned decisions (before any code)

Small, written, reviewable — each is a section appended to `Lambda_Design_Concurrency.md` or a short note:

- **P0.1 — O10 loop-ordering spec.** One page: the interleaving of uv callbacks, JS microtask flushes, and Lambda task resumes. Pinned rule from §4.6: task resumes enqueue at **macrotask position** (after the current job's microtask drain). Must also cover: resume ordering among multiple ready tasks (FIFO by readiness), and where `send`-completions and handle-completions slot in (K20e: completion strictly after that task's sends).
- **P0.2 — O9 may-await closure rules.** Seeds = builtins flagged async in the registry (`wait`, `receive`, `select`, `sleep`, v1 async `io.*` set) + native-module signatures marked async (K16). Propagation along static call edges; **indirect `pn` calls (closures/HOFs, `pn`-valued variables) are conservatively may-await**; `fn` exempt by construction. Output: per-pn bit consumed by the transform (Phase 3) and the JS membrane (Phase 4). Include the diagnostic format ("this `pn` suspends because it calls …").
- **P0.3 — Handle representation (O6, v1 slice).** v1: an opaque, immutable **handle value** (native-backed Map/VMap per the module-design pattern, or a small new tagged type — decide with a 1-pager; lean VMap for GC-finalizer reuse). Handles are identities (the ref-cell family) — `==` on handles is identity; full typing deferred with O6.
- **P0.4 — Pinned constants & policies.** Default mailbox bound (proposal: 1024 messages); `send` full-mailbox error code; **script-exit semantics**: when `main` returns, the runtime drains the loop until no live tasks remain (Node's exit-when-loop-empty precedent; the bounded-drain machinery in `js_event_loop.cpp` "Js57 P2c" is the in-tree pattern), with a `--no-drain` escape hatch for tests.
- **P0.5 — Grammar sketch for `start`** (O7 slice): contextual keyword, statement/expression-initial, `pn`-only; operand = a `pn` call expression. Confirm no conflict with `start` as identifier (corpus uses it as param/key — contextual handling in `grammar.js`).
- **P0.6 — K30 rules pinned for implementation**: the block-exit discipline (normal exit → join children; error exit → cancel-then-join — emitted on exit edges, the same lowering family auto-close (R-ledger) will use, and a `defer` keyword if ever introduced (R5-R): **build it shaped for that reuse**); the `'cancelled'` and `'timeout'` `T^E` error codes; the cleanup-masking flag semantics (K30d); handle-escape rules for scoping (R3-family: escape by return; escape into messages/`send` = ownership NOT transferred — the handle is a capability, the task's scope owner is unchanged — spell this out).

**Gate:** specs reviewed; no code yet.

## Phase 1 — Grammar, AST, and the two analyses

- **1.1 Grammar** (`lambda/tree-sitter-lambda/grammar.js` → `make generate-grammar`): `start_expr` per P0.5. No other syntax — everything else is builtins (K12).
- **1.2 AST** (`lambda/build_ast.cpp`): the `start` node; semantic checks:
  - `start` outside `pn` → compile error (matches `pn`-only builtins).
  - **K13 capture rule**: the `start` operand's closure must not capture `var`s by reference — reject with a targeted error (snapshot is NOT silently applied in v1; explicitness first). This lands in the same closure-analysis pass that `proc_closure_mutation` semantics live in.
- **1.3 May-await closure** (per P0.2) in the analyzer alongside purity inference; registry gains an `is_async` flag per sys-func entry (`lambda/sys_func_registry.c` table extension).
- **1.4 Handle-escape analysis (K30a, R3-family)**: per block, which started tasks' handles escape by return; non-escaping tasks get scope-exit join/cancel treatment (P0.6). Shares its shape with the future resource-escape analysis — one pass, two clients eventually.
- **Tests:** parse/AST positives; negatives under `test/lambda/negative/` (`start` in `fn`, `start` at top level of pure context, `var` capture into `start`); closure-analysis unit cases incl. indirect-call conservatism. *(CLAUDE.md rule 8: every new `.ls` ships with its `.txt`.)*

**Gate:** `make test-lambda-baseline` 100% (nothing existing may change); new negatives pass.

## Phase 2 — Runtime substrate: tasks, handles, mailboxes, scheduler (no transform yet)

Buildable and testable from C before the transform exists (drive it from gtest):

- **2.1 Task + handle**: task struct (state: runnable/parked/done, result Item, frame ptr, mailbox); handle per P0.3, exposing completion (`T^E`) and identity.
- **2.2 Bounded mailbox**: ring buffer of Items, bound per P0.4; enqueue (fails when full → the `ok^E` surface), FIFO-head dequeue; **no in-queue matching exists** (K20c is enforced by API shape — there is no skip primitive to misuse).
- **2.3 Scheduler**: per-context run queue on the K3 loop. For contexts with JS: attach to the existing uv loop (`js_event_loop.cpp`); for pure-Lambda scripts: bring up the same uv loop in `runner.cpp` (this is the piece that makes `lambda.exe script.ls` loop-capable — new lifecycle code, P0.4 exit semantics apply).
- **2.4 K20e ordering**: a task's completion event is published strictly after its final sends are enqueued (single-threaded per context makes this a sequencing discipline, not a lock problem — but write the assertion).
- **2.5 GC rooting**: scheduler structures (run queue, parked set, mailbox contents, task results) registered as a root set; frames and mailbox Items trace as ordinary heap objects (the K2-R dividend — no stack scanning).
- **2.6 Builtins registered** (`sys_func_registry.c`, all `pn`, async-flagged where applicable): `send(h, msg) -> ok^E`, `receive() -> item` (parks), `wait(h[, timeout:]) -> T^E` (parks; timeout errors the waiter, never cancels — K30f), `select(h1, h2, ..., timeout: ms)` (function-call form only, O4 syntax deferred), `sleep(ms)`, `self() -> handle`, **`cancel(h)` (K30c/e: sets flag, unparks pending suspension with `T^E 'cancelled'`; idempotent; any holder)**.
- **2.7 Cancellation plumbing (K30)**: task struct gains cancel flag + cleanup-masking bit; park sites check the flag on entry and on unpark; completion of a cancelled task follows K20e ordering like any other completion.

- **Tests:** gtest suite for queue semantics, bound behavior, FIFO, K20e ordering (deterministic — driven by explicit scheduling, no timing races).

**Gate:** runtime gtests green; baseline untouched.

## Phase 3 — The state-machine transform (`transpile-mir.cpp`) — the core

Per K2-R/K14/K17, **following the Phase-6 pattern** (`js_mir_function_class_lowering.cpp`) as a second, initially independent implementation — shared-core extraction comes later:

- **3.1 Split points**: calls to async builtins + calls to may-await `pn`s (per the O9 bit). Live-local analysis across each split; locals hoisted to a heap frame (state index + Item slots).
- **3.2 Calling convention**: calls **to** may-await `pn`s return `value | SUSPENDED`; callers in the closure branch on it (park self, chain resume). `pn`s **outside** the closure and all `fn`s compile *exactly as today* — this containment is the baseline-safety story.
- **3.3 `start` lowering**: allocate task + frame for the target `pn`, enqueue runnable, return handle. Direct calls to may-await `pn`s = inline suspend-propagation (no task, no handle — the colorless fast path, zero allocation when nothing suspends).
- **3.4 Errors**: `T^E` values flow through frame slots; `^`-propagation compiles as ordinary branches across states — **no exception-region machinery** (the simpler-than-JS clause of K17).
- **3.5 Debuggability**: distinct log prefixes per CLAUDE.md; state-machine dump readable via `temp/mir_dump.txt` workflow; a `log_debug` trace mode for park/resume.
- **3.6 Scope-exit lowering (K30a/b)**: at each block exit edge, emit join (normal path) / cancel-then-join (error path) for non-escaped started tasks, per the 1.4 analysis. **This is the first user of the exit-edge lowering machinery that auto-close (R-ledger) will share — build the emission helper generic from day one** (no `defer` keyword is planned initially, R5-R; the helper serves it too if it ever arrives). Masking (K30d) wraps the cancel-path joins.
- **Tests** (`test/lambda/conc/*.ls` + goldens): start/wait round-trip; multi-task interleaving via message ordering (deterministic — assert on sequences, never on timing); send→ok^E on full; K20e end-after-sends; deep colorless chains (b awaits through 5 unmarked frames); error propagation through `wait`; `select` basics; **K30 suite**: scope-exit join on normal exit, cancel-then-join on `^`-error exit, `cancel(h)` while parked / before first run / after completion (idempotent), cleanup-masked cancellation (via scoped-task cleanup), `wait` timeout without cancellation, escaped-handle tasks surviving their birth block.

**Gate:** conc suite green; **`make test-lambda-baseline` 100%** (the load-bearing gate — non-concurrent code must be byte-identical in behavior); release-build perf smoke (spawn/send/wait microbench, indicative only).

## Phase 4 — JS async interop (the unification, K10/K16/§4.6)

- **4.1 Lambda awaits a JS Promise**: `wait(promise)` parks the task and registers a promise reaction that re-enqueues the resume at macrotask position (P0.1). Rejection arrives as a `T^E` error value (J3 — no exception crosses).
- **4.2 JS calls a may-await `pn`** — the membrane (K16): **every Lambda `pn` exposed to JS returns a Promise, uniformly** — wrapper dispatches a fresh task and resolves/rejects on handle completion. Rollout decision inside this phase: apply to new exports first; audit existing `js_dom`-bridge `pn` surfaces before flipping them (regression-gated).
- **4.3 Handles ↔ Promises**: `toPromise(handle)` adapter (and the membrane uses it); a JS Promise is `wait`-able from Lambda (4.1) — the two parking mechanisms meet only at the loop, per §4.6's layer table.
- **4.4 Shared loop verification**: one uv loop per context, JS jobs + Lambda resumes interleaved per P0.1; v1 async host ops routed through the existing uv services — **O8 v1 set: timers (`sleep`) + one file op (`io.read` async) as the proof**, the rest with the streams work.
- **Tests:** mixed-language suites (JS awaiting Lambda, Lambda waiting on fetch-style promise chains, ordering assertions vs microtasks); **gates: node baseline unchanged, editor JS suite (1931) green, UI-automation baseline unaffected.**

**Gate:** all JS suites green; cross-language ordering tests deterministic.

## Phase 5 — Hardening, docs, and the extraction decision

- **5.1 Docs**: `doc/Lambda_Sys_Func.md` (six builtins), `doc/Lambda_Procedural.md` + `doc/Lambda_Reference.md` (`start`, colorless model, capture rule), cheatsheet entry; a short "concurrency" section in the LR-series runtime docs.
- **5.2 Diagnostics polish**: the O9 "suspends because it calls …" explain; capture-rule error fix-its (suggest `let` snapshot or message passing).
- **5.3 Shared-core extraction (K17 step 2) — decision point, not a commitment**: with two working state-machine clients (JS Phase 6 + Lambda Phase 3), assess extracting the neutral MIR "resumable function" utility. Proceed only if the shared shape is clean; gated on the full JS suites. If deferred, record why.
- **5.4 Follow-on queue (design-first, in order):** the process tier (K18 + K29 text wire); streams executor (P8 + K21–K28, incl. the K27 chunk-queue core — **its K26 prerequisite is satisfied: K30 designed, and implemented by this plan**); **auto-close implementation** (R-ledger — reusing the 3.6 exit-edge machinery; no `defer` keyword initially per R5-R, introduced only on pressing need); Stage A parallel-`fn`.

## Cross-cutting risks

| Risk | Mitigation |
|---|---|
| Calling-convention change leaks outside the may-await closure → baseline regressions | Containment by construction (3.2); baseline gate at every phase; closure membership dumped in debug builds for audit |
| O9 over-conservatism (indirect calls) balloons the closure → needless transforms | Measure closure size across `test/lambda` + benchmark corpus in Phase 1; diagnostics show *why* each `pn` is included; acceptable v1 outcome, optimize later |
| Pure-Lambda scripts gain a loop lifecycle (`runner.cpp`) → exit-behavior surprises | P0.4 spec first; Js57-P2c bounded-drain pattern reused; explicit tests for exit-with-pending-tasks |
| Timing-dependent flaky tests | House rule for the conc suite: assert on message *orderings and values only*; `sleep` only where the assertion is about `sleep` itself |
| Membrane flip of existing `js_dom` pn surfaces breaks pages | 4.2 rollout order: new exports → audited flip, each step behind the JS/UI-automation gates |
| GC: frames/mailboxes missed by root scan | 2.5 root set + a stress gtest (spawn/park/collect loops) before Phase 3 lands |
| Normal-exit join hangs (child never completes) | Documented Trio-accepted behavior; guidance: `wait(h, timeout:)` + `cancel(h)` idiom; CPU-bound cancellation blindness documented (K30c limit); loop safepoints as recorded future fix |

## Phase gates summary

| Phase | Ships | Hard gate |
|---|---|---|
| 0 | O10 + O9 specs, P0.3–P0.5 decisions | review |
| 1 | `start` grammar/AST, capture rule, may-await analysis | baseline 100% + negatives |
| 2 | tasks/handles/mailboxes/scheduler + 6 builtins | runtime gtests; baseline 100% |
| 3 | state-machine transform, colorless end-to-end | conc suite; **baseline 100%**; perf smoke |
| 4 | JS membrane + shared-loop interop | node baseline · editor 1931 · UI-automation unchanged |
| 5 | docs, diagnostics, extraction decision | doc build; K17 assessment recorded |

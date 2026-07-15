# Lambda Stack Frame — JS Runtime Open Design Issues (JO ledger)

**Status:** OPEN ledger — nothing here is decided yet; issues to be tackled one by one.
**Date:** 2026-07-15
**Context:** Successor ledger to `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20, OS1–OS11 all closed). With the Lambda phase-1 implementation complete and the JS phase (J1–J3) largely landed, the JS detailed-design docs (`doc/dev/js/JS_01`–`JS_16`) were swept for stack-frame-related issues **not owned by any of the three implementation plans**:

- `vibe/Lambda_Impl_Stack_Frame.md` — phase 1, Lambda MIR-Direct + shared runtime
- `vibe/Lambda_Impl_Stack_Frame_JS.md` — phase 2, JS transpiler/runtime onto side stacks
- `vibe/Lambda_Impl_Stack_Frame_JS2.md` — SF15-J, JS-array `extra`/props migration

Already tracked elsewhere and therefore **not** in this ledger: broad C-helper migration to the RAII root guard and conservative-scan retirement (phase-2 §1 "Out of scope (phase 3+)"); `js_args_stack` unification (explicitly keep-as-is unless profiling says otherwise); `js_await_sync` real suspension *architecture* (owned by the concurrency K-series / SF20 — but its JS-doc symptom is recorded here as JO9 so it isn't lost). Doc-staleness ("nursery" vocabulary in JS_01/04/09/15/16 and LR_01/03/04/09) was fixed 2026-07-15 alongside this ledger — not an open issue.

Sources for each item: the doc's Known Issues section, cross-checked against code where cited.

---

## Group A — Root-registration de-pinning (the un-de-rooted siblings of J2)

Phase 2's J2 stage cured exactly one instance of a general disease: object graphs pinned forever because a pool, table, or cache is registered with the GC wholesale and never released. Closure envs were the biggest case; these remain. The uniting design question — the natural core of a **phase 3** — is: *what is the JS analog of "envs became ordinary GC-traced objects"?* Likely shape: records become GC-heap objects traced from their wrapper Items; root registration is reserved for true globals; per-item `heap_register_gc_root` is banned on growth paths (it re-creates the O(n²) registration/marking cost the args-stack fix killed, `js_runtime_function.cpp:38`).

**JO1 — Promise pool is wholesale-rooted and never freed.** `js_promises[JS_MAX_PROMISES=1024]` is registered with the root scanner once (`js_promise_register_roots_once`, `js_runtime.cpp:28160`); `js_alloc_promise` bump-allocates and slots are never freed — every settled promise's `result`/handlers/`next_promise`/`wrapper` stay pinned for the heap's lifetime, and long-running programs leak promise records (JS_09 §2, KI-4). Adjacent caps ride the same fix: per-promise reaction arrays capped at 8 (`then_count < 8` silently drops further `.then`s, `js_runtime.cpp:29444`), `JS_MAX_ASYNC_CONTEXTS` 256, `JS_MAX_MODULES` 64, `JS_TLA_MAX_CONTINUATIONS` 64.

**JO2 — Generator pool has no GC reclamation.** `js_generators[4096]` (`js_runtime.cpp:26562`) never frees a slot; exhaustion recycles only the oldest `done` slot, so >4096 live generators or heavy churn can collide indices or fail allocation. The doc's own improvement note: "tie generator records to GC lifetime" (JS_08 KI-5).

**JO3 — Async-context table re-pins de-rooted envs.** The fixed 256-entry async-context table "roots suspended env pointers without adding a root range per environment" (JS_05 §Closure creation) — suspended envs, freshly de-rooted by J2, are pinned wholesale through a capped static table while suspended. Needs the SF20 shape: suspension state as a GC-owned object whose Item region is traced, not a static root table.

**JO4 — Per-module var arrays accumulate permanent root ranges.** Every nested `require()`/`import()` `pool_calloc`s a fresh 2048-slot array and registers it as a GC root range (`js_alloc_module_vars`, `js_runtime_state.cpp:159`; JS_03 §11) with no unregistration path — module churn in a long-running process grows the root-range list without bound. Phase 2 declared module vars "globals row, correct as-is," which holds for the *static* array only. Riders: the vestigial `js_save/restore_module_vars` pair (JS_09 KI-5, confirm-and-delete) and the hard 2048 cap with silent out-of-range drops (JS_03 KI-4).

**JO5 — Per-item root registrations on growth paths.** Three subsystems register a GC root per created object: timers root each callback + extra args (`timer_register_gc_roots`, `js_event_loop.cpp:822`); the DOM wrapper identity cache registers **every cached Item** as a root and scans its chunks linearly (`js_dom.cpp:846`/`:856`; JS_13 §Identity cache + KI-6); Node namespace caches register each namespace (`js_get_vm_namespace` pattern, `js_runtime.cpp:31456`). Namespaces are bounded (fine as globals); timers and DOM wrappers are unbounded growth paths recreating the pre-args-stack O(n) registry problem.

**JO6 — Cached compiled wrappers are module-lifetime.** The cacheable `js_new_function` path keeps pooled `JsFunction` wrappers because the function cache embeds their addresses; uncached wrappers/closures/bound functions are GC-owned, but repeatedly compiling distinct modules retains cached wrappers until module teardown (JS_03 KI-3). The softer residue of the old "JsFunction never freed" debt — needs a cache-invalidation/lifetime story rather than rooting work.

## Group B — Suspension-state machinery (SF20-adjacent)

The *re-homing* half of suspension is done — generator/async entries run the frame discipline, "suspension state owns no pointer into a reclaimed invocation" (JS_04 §5). What no plan owns is the fragile machinery that discipline rides on:

**JO7 — 64-state cap + yield-counting heuristics are correctness-load-bearing.** `gen_state_labels[64]` (`js_mir_context.hpp:460`), yield count clamped to 63 (`js_mir_function_class_lowering.cpp:621`) — a generator with more syntactic yield/await points **silently loses resume labels** (`jm_transpile_yield` returns undefined past the cap). `jm_count_yields` is acknowledged to over-count (`:1208`, defensive dead labels emitted) *and* patterns can under-count — which is exactly why the `>= 64` fallback exists (JS_08 KI-1/2). Await shares the same 63-state budget (KI-4). Fix direction: size the label array from the pre-scan and make the count exact (or make over-count harmless *and* under-count impossible), rather than heuristic-plus-cap.

**JO8 — Generator vreg spill is hand-rolled compensation for the register allocator.** Spilling live values around `yield` resume edges is scattered, manual, and "error-prone" — compensating for MIR's RA not preserving vregs across the resume edge (JS_04 KI-3). Design question: teach the state-machine transform a principled liveness/spill model (K2-R splitting identifies suspension-point-live locals — same machinery SF20 wants for write-at-suspend optimization) instead of per-construct patches.

**JO9 — `js_await_sync` cannot suspend.** A pending await outside a state machine falls back to a bounded busy-drain and returns `undefined` if it cannot settle in-turn (`js_event_loop.cpp:1158`; JS_09 KI-6). Architecture owned by the concurrency K-series (real async frames, SF20); recorded here so the JS-side symptom isn't orphaned until that implementation plan exists.

## Group C — C-stack growth (distinct from side-stack exhaustion)

**JO10 — TCO gaps leave the C stack unprotected where OS8 protects the side stacks.** TCO fires only for self-recursion (`jm_has_tail_call` handles return/block/if only) with a silent `tco_count ≤ 1,000,000` cap returning 0 on overflow (`js_mir_function_class_lowering.cpp:413`); mutual/general tail calls grow the C stack; and MIR **interpreter** link mode has no TCO at all — a deliberate JIT/interpreter divergence where deep recursion overflows only under the interpreter (JS_05 KI-5; JS_01 §6). OS8 made *side-stack* exhaustion clean and fail-fast; the C-stack story for tail chains is still cap-and-hope.

## Group D — Recovery-path residue (SF17-adjacent)

SF17's watermark-restore contract holds at the three recovery boundaries; these are the resources and audits watermarks don't cover:

**JO11 — MIR code pages leak ~55 MB per crash recovery.** Each SIGSEGV/timeout `longjmp` in the batch worker leaks the JIT'd code pages; bounded only by the 10-crash / 4 GB RSS worker exit (JS_16 KI-3). Needs a reclaim (or arena) story for MIR code memory at the recovery boundary — the code-image side of what SF17 did for the side stacks.

**JO12 — Event-loop SIGSEGV band-aid + un-audited timer re-entry.** `js_event_loop_drain` installs a SIGSEGV handler + `setjmp` to survive "heap corruption in timer callbacks (pre-existing bug)" (`js_event_loop.cpp:1076`; JS_09 KI-7) — a masked memory-safety bug, not a fix. Related audit gap no plan stage covers: timer callbacks re-enter a **captured** runtime via a scratch `EvalContext` (`timer_runtime_enter`, JS_09 §5) after the originating context may have been swapped out — verify the re-entry binds/saves the current thread's side-stack watermarks correctly (the callback must run as a proper top-level frame, and a mid-drain recovery must restore the drain's watermarks, not the callback's). Plausibly JO12's corruption and this audit are the same bug.

## Group E — Boxing-traffic codegen (the other half of JS_15 §5.1)

**JO13 — Float register residency / scalar replacement.** The side stacks retired the *allocation* half of float boxing (inline doubles + frame-reclaimed residue slots); the *traffic* half remains: shaped float fields round-trip through boxed Items on every read/write instead of staying register-resident across loop iterations — still the dominant residual on nbody/matmul/mandelbrot/spectralnorm (JS_15 §5.1, roadmap #1). Rider: native multiply routes INT×INT through `I2D`/`DMUL`/`D2I` to match JS semantics, so pure-integer hot loops pay double-arithmetic cost (JS_04 KI-6). A pure codegen project (scalar replacement of aggregates / shaped-slot unboxed storage), independent of the memory architecture.

---

## Suggested tackle order

1. **Group A (JO1–JO5)** — one design decision covers five issues: the "GC-owned records, roots for true globals only" doctrine, then per-subsystem migration mirroring J0a census → J2 de-rooting. JO6 rides the function-cache lifetime story.
2. **JO12 audit first, then JO11** — the timer re-entry watermark audit is cheap and may root-cause the masked corruption; the code-page reclaim is bounded, standalone work.
3. **JO7 before JO8** — exact state counts are a contained fix; the principled spill model can then reuse K2-R liveness when the concurrency work lands. JO9 waits on that same work.
4. **JO10 and JO13 last** — separate codegen projects (TCO generalization; scalar replacement) with their own cost/benefit profiles, no memory-architecture dependency.

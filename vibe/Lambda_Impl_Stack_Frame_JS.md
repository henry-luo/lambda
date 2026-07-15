# Lambda Stack Frame — Implementation Plan, Phase 2 (JS)

**Status:** draft v1 — starts after phase 1 stage 3 has soaked
**Date:** 2026-07-15
**Design:** `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20). **Phase 1:** `vibe/Lambda_Impl_Stack_Frame.md` (Lambda MIR-Direct + shared runtime). This plan brings the JS transpiler and JS runtime onto the same architecture.

---

## 1. Where phase 1 leaves JS, and why phase 2 pays

After phase 1, JS code runs **correctly but on legacy footing**: its numbers home into the exec base frame (nursery-equivalent, monotonic per run) because JS emits no watermark frames, and its rooting is what it has always been. Verified current state of that footing (2026-07-15):

- **The JS transpiler emits no per-frame rooting at all** (no `heap_jit_gc_root_frame_*` calls anywhere in `js_mir_*`). JS-generated code's locals are protected only by the **conservative C-stack scan** — the same under-retention hole (R-I5) that produced BUG-001 on the Lambda path is latent in JS.
- **Closure envs are permanent GC root ranges**: `js_alloc_env` (`js_runtime_function.cpp:251`) pool-allocates each env and registers it as a root range that is **never unregistered**. Every env ever created — and everything reachable from it — is pinned for the life of the heap. The root-range list grows without bound; `js_runtime_function.cpp:52` documents the O(n²) root-range scanning this already caused for call arguments.
- **The args stack already implements the SF6 pattern**: `js_args_push`/`js_args_save` is a watermarked bump stack (stable base, save/restore marks, zeroed-above-top GC invariant, registered once). It stays as-is; phase 2 generalizes its idea rather than replacing it.
- Module-var arrays and runtime singletons (`js_current_this`, `js_exception_value`, …) are registered roots — module/session lifetime, correct as-is (the OS1 globals row).

**Phase 2 payoffs, in order of value:**
1. **Precise rooting for JS locals** — closes the latent BUG-001-class hole.
2. **Env de-rooting** — envs become ordinary GC-traced objects; the permanent-pin over-retention and unbounded root-range growth end. This is JS's biggest memory win.
3. **Frame-scoped numbers** — long-running JS (servers, the event loop) stops accumulating boxed numerics per run; each callback invocation reclaims at return.
4. Retirement of the now-unused heap `JitGcRootFrame` machinery and completion of the two-transpiler symmetry the unified-AST effort assumes.

**Scope note (small by design):** JS's wide-scalar traffic is only out-of-band doubles plus polyglot int64/DateTime — JS numbers are inline doubles (SF18) and BigInt is `integer`/decimal (OS1). And JS containers are Lambda containers, so **SF15/SF16 re-homing is already live for JS** the moment phase 1 stage 3 lands (the `fn_*`/container helpers are shared). Phase 2's re-homing surface is just: returns, env tails, and the exception slot.

**Out of scope (phase 3+):** broad C-helper migration to the RAII guard; conservative-scan retirement (needs that migration); js_args_stack unification into the side-stack family (works fine as-is; revisit only if profiling says so).

---

## 2. Stages

### Stage J0 — Audit and inventory (no behavior change)

- **J0a. Rooting census**: enumerate every `heap_register_gc_root(_range)` in `lambda/js/` with lifetime class (permanent / module / heap-reset). Instrument a node-baseline run to count live env root ranges over time — quantifies the over-retention win before touching anything.
- **J0b. Generator/async state map**: confirm where generator and async-function state lives (JsFunction flags `JS_FUNC_FLAG_GENERATOR`/`ASYNC_GEN`, `js_runtime_function.cpp:258+` — state presumed env-based; verify). If generator state is env-resident, SF18 env tails cover suspension automatically and no separate SF20 analog is needed for JS; verify the same for promise reaction closures in `js_event_loop.cpp`.
- **J0c. Bridge census**: enumerate JS↔Lambda call-bridge sites (Jube). Confirms/updates phase 1's 3g shim inventory from the JS side.
- **J0d. Return-shape decision input**: count JS call-emission sites that would change under two-lane protos, vs. the TLS-slot fallback (see J3a). Exit: facts recorded; decision made.

### Stage J1 — JS frames + side-stack rooting (flag: `JS_SIDE_STACK_ROOTS`)

- **J1a.** `JsMirTranspiler` emits the **same `em_*` frame primitives** phase 1 added to `mir_emitter_shared.hpp` — this is the U21/Phase-0 dividend; no new emission machinery. Prologue saves the root watermark + bumps by the static count; unified `VarEntry.root_slot` (P0.3) carries the slot index; rooted stores are inline register-relative stores.
- **J1b. Single-epilogue discipline in JS lowering.** JS exceptions are already TLS-value + error-return (no unwinding), and `try/catch/finally` lowers to normal control flow — so this is the same restructuring as phase 1's 1c, applied to `js_mir_expression_lowering.cpp`'s return paths.
- **J1c. JS rooting predicate**: objects/strings/functions/symbols root; numbers never (inline doubles); `ANY`-typed roots. Env pointers held in locals root as pointers — which is what makes J2 possible.
- **J1d.** Recovery-site coverage: JS runs under the same three SF17 boundaries (shared runner) — verify no JS-specific recovery path exists outside them (watchdog kit interaction, JT4b).
- Gates (§3) + soak; flag default-on.
- **J1e.** Delete the heap `JitGcRootFrame` machinery from `lambda-mem.cpp` if phase 1 has not already (JS never used it — phase 1 doc corrected accordingly; after both transpilers emit side-stack rooting it has zero users).

### Stage J2 — Env de-rooting (JS's own debt, enabled by J1)

- `js_alloc_env` → GC-heap allocation (`heap_calloc`), traced precisely through the owning `JsFunction`'s closure-env reference and parent-env chains (the Lambda `closure_env` trace path in `gc_heap.c` already exists — extend to JS env chains). **No more root-range registration per env; no more permanent pinning.** Envs die when their closures die.
- Prerequisite from J1: locals holding function/env pointers are precisely rooted, so reachability is complete without the conservative crutch.
- J0a's census verifies every env consumer traces (DOM handlers, timers, promise reactions hold JsFunction refs — audit that nothing holds a bare env pointer outside a traced object).
- Gate additions: node-baseline memory profile shows env count now tracks live closures (vs. monotonic before); no resurrection/UAF under GC-stress + ASan on the JS suites.

### Stage J3 — JS frame-scoped numbers + the small re-homing surface (atomic, like phase 1 stage 3 but far smaller)

- **J3a. Returns**: adopt SF14 `[item, scalar]` for JS→JS calls via the shared emitter (default — uniformity with Lambda, same machinery); fallback option if J0d shows unacceptable proto churn: a single TLS return-slot re-home (callee writes payload, caller re-homes immediately at the call return — safe because a return value is singular and consumed before any other call). Decide in J0, implement one.
- **J3b. Env tails** (SF18): capture and write-back of wide scalars target the env's own tail. Per J0b, this also covers generator/async suspended state if it is env-resident.
- **J3c. Exception slot**: a thrown wide scalar (rare — out-of-band double) re-homes into the base-frame extent when written to `js_exception_value` (globals-row treatment); alternatively wrap per the error=map rule. Pick during implementation; either is a one-liner at the throw helper.
- **J3d. Frame number watermarks on** for JS frames; JS event-loop callbacks now reclaim numbers at every return.
- Container copy-in/copy-out: **nothing to do** — shared helpers, live since phase 1 stage 3.
- Gate additions: **long-running event-loop soak** (server workload — steady-state RSS, numbers reclaimed per tick); node/js baselines; polyglot int64 round-trip tests through the J0c bridges.

### Stage J4 — Cleanups and closure

- **J4a.** OS5 JS profile constants from J-stage telemetry (JS's number-stack budget is structurally thinner — encode it in the profile matrix).
- **J4b.** Remove any now-dead JS rooting shims found in J0a whose coverage became precise (module vars and singletons stay — they are the globals row).
- **J4c.** Doc + memory sync: LR/JS design docs updated to describe the two-transpiler symmetric architecture; `vibe/Lambda_Design_Stack_Frame.md` status updated to "implemented (Lambda+JS)".

---

## 3. Gates (every stage)

- `make node-baseline` — no regression from 1492/3517; JS gtest suite green; `make editor-4c-js` 1931/1931; DOM/editor view suites; the jquery/popper knowns tracked separately
- Full **Lambda** baselines byte-identical — the phase-2 contract mirrors phase 1's: the other language must be unaffected
- `RuntimeError_StackOverflow` fail-fast; deltablue; havlak+push; AWFY within noise (release)
- GC-stress + ASan runs on JS suites for J1/J2 especially (rooting changes)
- J2+: env-lifetime memory profile; J3+: event-loop soak steady-state RSS

## 4. Success criteria for phase 2

JS-generated code runs with precise side-stack rooting (conservative scan demoted to backstop for C helpers only), closure envs GC-collected instead of permanently pinned, numbers reclaimed per callback in long-running services, and the heap `JitGcRootFrame` machinery deleted — with Lambda behavior and baselines unchanged, and both transpilers emitting through the same `em_*` primitives ready for phase 3 (helper migration + conservative-scan retirement).

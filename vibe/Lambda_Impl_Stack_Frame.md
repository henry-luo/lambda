# Lambda Stack Frame — Implementation Plan, Phase 1 (Lambda)

**Status:** draft v1 — Lambda-first scope
**Date:** 2026-07-15
**Design:** `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20, all decided). This plan implements the design for the **Lambda MIR-Direct path + shared runtime**; the JS transpiler is phase 2 — `vibe/Lambda_Impl_Stack_Frame_JS.md`. C2MIR is untouched forever per OS6.

---

## 1. Scope boundary — what makes "Lambda first" safe

The load-bearing fact: **runtime boxing helpers are called by name** (`push_l`/`push_k`/`box_float_cold` and every `fn_*` that boxes). Their implementations flip runtime-wide, but frame *watermarks* only advance/pop where a transpiler emits them. Phase 1 emits frames **only in `transpile-mir.cpp`** (Lambda). Everything else — JS-generated code, C2MIR-generated code, host C helpers running under them — homes numbers into the **exec base frame** established by the runner, whose extent is the whole script run. That is exactly today's nursery lifetime, so non-Lambda code keeps status-quo semantics with zero code changes.

**In scope (phase 1):**
- Side-stack infrastructure in `lib/` (both regions, all three platforms — the Windows `VirtualAlloc` shim is ~30 lines, not worth deferring)
- GC integration: precise root-region scan; conservative scan stays; old heap `JitGcRootFrame` machinery **stays compiled** (JS still uses it until phase 2)
- Lambda MIR-Direct transpiler: watermark frames, inline root stores, single-epilogue restructure, SF14 two-lane returns for Lambda JIT→JIT calls
- The full Lambda re-homing surface: SF15 container copy-in, SF16 copy-out + `is_immortal` flag, SF18 env tails, SF20 async-frame tails, REPL base frame
- Nursery + `box_float_cold` retirement (runtime-wide, safe per the base-frame argument)
- Recovery-site watermark snapshot/restore (SF17's three longjmp sites)

**Out of scope (phase 2 = JS; explicitly deferred):**
- JS transpiler frames, watermarks, two-lane returns, env tails (JS numbers ride the base frame; JS rooting stays on heap `JitGcRootFrame`s)
- Conservative-scan retirement (needs helper migration + JS both complete)
- Broad helper migration to the RAII root guard (only the guard class + a pilot module land in phase 1)
- Per-task arenas, write-at-suspend liveness optimization, OS5 profile matrix beyond initial constants

**Coordination with in-flight work:**
- **Unified AST Phase 0 (MirEmitter)**: emit the new frame/watermark/root primitives as **`em_*` emitter functions in `mir_emitter_shared.hpp`**, not as `MirTranspiler`-local statics — phase 2 (JS) then inherits them through the shared emitter for free. This is the same interleave discipline as U21.
- **Concurrency Stage A** (`concurrency.cpp` in flight): SF20's tail extension modifies `LambdaAsyncFrame` — land stage 3 of this plan after (or with) the concurrency code stabilizes; the struct change is additive (slots block grows a tail).

---

## 2. Stages

Each stage lands alone and soaks on the full gates (§3) before the next begins — the P0.2 precedent.

### Stage 0 — Independent preludes (each its own PR, any order, can start now)

- **0a. SF10 small-int64 inlining.** int64 values fitting 56 bits pack inline in the Item (the `get_int56` mechanism with an int64-typed tag so `type()` fidelity is preserved); only genuinely wide values box. Shrinks number-stack traffic to a trickle before the architecture lands. Gate: lambda + JS baselines (JS BigInt egress paths touch int64 boxing).
- **0b. Concat payload-rebase fix** (`lambda-eval.cpp:350`): copy tail payloads into the result's own tail and rebase the Item pointers. Fixes the latent dangle *today*, and the copy-and-rebase helper it introduces is exactly the SF15-invariant primitive stage 3 reuses at every container write site. Gate: lambda baseline + a new regression test (concat wide-scalar arrays, drop source, force GC, read result).
- **0c. Slot-count instrumentation.** Transpiler logs per-function static root/number slot counts under a debug flag; run AWFY + baselines; the histogram sets OS5's initial constants. No behavior change.

### Stage 1 — Side-stack rooting for Lambda (flag-gated: `LAMBDA_SIDE_STACK_ROOTS`)

- **1a. `lib/` region module**: reserve/commit per SF13 (plain `mmap` on macOS/Linux; `VirtualAlloc` reserve+commit shim on Windows), TLS tops, watermark save/restore API, exhaustion → catchable stack-overflow error, `madvise` hook for the GC driver. Root region only in this stage.
- **1b. GC integration**: `heap_gc_collect` precisely scans `[root_base, root_top)` alongside the existing heap-frame walk and conservative scan. Both rooting mechanisms coexist (Lambda on side stack, JS on heap frames).
- **1c. Transpiler — single-epilogue restructure first** (its own commit): all return paths in generated code funnel through one epilogue block per function, ordering = scope cleanup → (later: watermark restore) → return. This is pure control-flow restructuring with today's machinery, independently verifiable, and it is the SF17 invariant made structural.
- **1d. Transpiler — frames**: prologue saves the root watermark + bumps by the static count (one overflow check per frame); rooted stores become inline register-relative stores at fixed offsets; epilogue restores. Emitted as `em_*` primitives. Functions with zero rooted slots emit nothing. Async-slotted vars keep their current double bookkeeping (cleanup is stage 3).
- **1e. Recovery sites**: snapshot/restore the root watermark at the three SF17 boundaries (`runner.cpp` exec sigsetjmp; batch crash/timeout; batch MIR-error). Exec establishes the **base frame**.
- Flag flips default-on after gates + soak. Old heap-frame emission path in `transpile-mir.cpp` is deleted at stage end — **and so is the runtime machinery** (`JitGcRootFrame` in `lambda-mem.cpp`): verified 2026-07-15 that the JS transpiler never emits `heap_jit_gc_root_frame_*` calls (JS locals rely on the conservative scan + permanent root ranges; see the phase-2 plan §1), so Lambda was its only user.

### Stage 2 — Number stack infrastructure, nursery-equivalent mode

- **2a. Number region** comes up in `lib/` (second mapping, SF12).
- **2b. Flip the boxing helpers**: `push_l`/`push_k` bump the number-stack top; `box_float_cold` → number stack. **No Lambda frame advances or pops the number watermark yet** — everything homes into the base frame, so semantics are exactly nursery semantics (monotonic per run, freed at exec exit). `gc_nursery` is deleted in this stage; `INT64`/`DTIME` stay in the rootable set for now (their Items point into the number region, which isn't GC memory — verify the scan treats them benignly).
- This stage is deliberately boring: it proves the region, the growth path, the recovery resets, and the REPL persistent base frame (which lands here), with zero lifetime risk. Long-run memory test: per-run ceiling replaces the old per-process leak — the N-I1 win is measurable already.

### Stage 3 — Frame-scoped numbers + the full re-homing surface (the big one, atomic for Lambda)

All of the following must land together, because the moment any Lambda frame pops the number watermark, every outliving store must already re-home:

- **3a. SF14 two-lane returns** for Lambda JIT→JIT: `[item, scalar]` protos for ANY-returning functions, `[scalar, error]` for native `^E` (removes the `can_raise → ANY` deopt at `transpile-mir.cpp:11525`, retires `INT64_ERROR` sentinels). Callers re-home returned wide scalars only when they escape.
- **3b. SF15 copy-in** at every Lambda container-write site (index-assign, push/splice, map/element construction, list building) using the 0b helper; container tail sizing on growth.
- **3c. SF16 copy-out** at every boxed-scalar read site (`array_get`, `list_get`, `map_get`, `elmt_get`, iteration); `is_immortal` container flag set by input parsers and the const-pool builder; immortal path returns references.
- **3d. SF18 env tails**: capture-time and write-back boxing target the env's own tail.
- **3e. SF20 async-frame tails**: `LambdaAsyncFrame` allocation grows the static tail; only the Item region is root-ranged; suspension-barrier invariant holds by construction.
- **3f. Frame-scoped watermarks on**: Lambda prologues/epilogues save/bump/restore the number watermark alongside the root watermark.
- **3g. Cross-language shim**: Lambda functions callable from JS (Jube bridges) get a single-lane wrapper that re-homes the two-lane return into the caller's (base-frame) extent. Audit call-bridge sites first; if none exist yet, record the requirement and skip.
- **3h. SF11 tightening**: `INT64`/`DTIME`/`FLOAT` leave `should_gc_root_var`; scalars are now fully out of GC on the Lambda path.
- ⚠️ Verification item folded in: **temporaries across mid-expression `wait`** — confirm state-machine spilling covers them before 3e ships.

### Stage 4 — Cleanups and pilot migration

- **4a. RAII root guard** (C+ class per SF8) in the module header + **pilot migration of one helper module** (suggest `lambda-data-runtime.cpp` getters) to validate ergonomics; broad migration is phase 2+.
- **4b. Async root-slot redundancy**: async-slotted vars skip side root slots (the GC-registered frame is their root).
- **4c. Vocabulary**: rename/re-document `push_*` (OS10) — the third naming era, matching mechanics at last.
- **4d. OS5 constants** finalized from stage-0c + soak telemetry; decommit cadence wired into the GC driver.
- **4e. OS9 audit** (scalar pointer-identity assumptions) and **OS11** (MIR-interp frame parity) close out.

---

## 3. Gates (every stage)

- `make test-lambda-baseline` 100%; full lambda gtest; JS + node baselines unchanged (JS must be *unaffected* — that is the phase-1 contract)
- `RuntimeError_StackOverflow` fails fast (never hangs, never balloons)
- deltablue green; havlak+push BUG-001 repro green (the historical GC-rooting tripwires)
- AWFY within noise on **release** build (stages 1, 3 especially — rooted stores and boxing are on hot paths)
- Stage 2+: per-run memory ceiling test (nursery leak must be gone and stay gone)
- Stage 3: new regression tests — wide scalars through every table row (return/container/env/async/error/REPL), plus the 0b concat test

## 4. Success criteria for phase 1

Lambda MIR-Direct code runs with: zero C calls for rooting (inline stores), zero rootless-frame overhead, scalars fully out of GC, nursery deleted, `^E` native returns un-deopted — with JS behavior and baselines byte-identical, riding the base frame until phase 2.

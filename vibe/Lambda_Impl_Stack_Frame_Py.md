# Lambda Stack Frame — Implementation Plan, Phase 3 (Python)

**Status:** draft v1
**Date:** 2026-07-15
**Design:** `vibe/Lambda_Design_Stack_Frame_Python.md` (PS1–PS10 decisions, PO1–PO9 ledger), over `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20, implemented for Lambda + LambdaJS). Prior phase records: `vibe/Lambda_Impl_Stack_Frame.md` (phase 1), `vibe/Lambda_Impl_Stack_Frame_JS.md` (phase 2), `vibe/Lambda_Impl_Stack_Frame_JS2.md` (SF15-J).

---

## 1. Scope and strategy

Python is the smallest SF port of the three front-ends (design doc §2): no DateTime traffic, no `push_l`, floats already inline via the shared `push_d`, containers plain `Array`/`Map` with zero `extra` uses. The work is: frames + rooting, env/generator ownership, args off `MIR_ALLOCA`, and boundary hygiene — reusing the JS phase's machinery wholesale rather than building a third implementation.

**Phase-3 contract** (mirrors phases 1/2): Lambda **and** JS baselines byte-identical at every stage — two implemented runtimes are now protected, not one. Python's own gate is `make test-jube` (the polyglot suite owning `test/py/`'s ~40 fixture/golden pairs) plus `test/test_py_gtest.exe`.

**Payoffs, in order of value:**
1. **Precise rooting for Python locals** — closes the latent BUG-001-class under-retention hole (P-I1); today Python is conservative-scan-only.
2. **Env/generator ownership** — retires the undeclared "py_input pool is never reset" invariant (PO2) that currently *is* the rooting; unblocks eventual SF9 conservative-scan retirement (Python is otherwise a permanent blocker).
3. **Args off `MIR_ALLOCA`** — retires the documented ARM64 MIR-inlining landmine (PO3) and per-call native-stack growth.
4. **Frame-scoped numbers + recovery boundary** — per-run numeric accumulation ends; `py_main` gets SF17 coverage it has never had.

**Out of scope:** type inference (PO8 — rides the unified-AST guest port; PS2's root-everything predicate is the phase-3 stance); concurrency/isolate readiness beyond making singletons `__thread` (PO7's full story rides the K-series globals ledger); `doc/dev/Python_Runtime.md`'s content beyond the P4 rewrite.

---

## 2. Stages

### Stage P0 — Preludes and audit (bug fix + censuses; no architecture change)

- **P0a. Fix PO1 (standalone, lands first).** `pm_box_int_reg`'s overflow branch (`transpile_py_mir.cpp:562–565`) emits `ITEM_ERROR`; reroute it to the runtime promotion path (the helpers already promote, `py_runtime.cpp:816`). Root-cause comment at the fix point (CLAUDE.md rule 12). New fixture + golden (rule 8): `test/py/test_py_int_overflow.py` exercising JIT-inlined `+`/`-`/`*` across the int56 boundary in both directions (e.g. `(2**54) * 4`, `-(2**55) - 1`) and verifying BigInt results, not errors.
- **P0b. Multi-`RET` census** for the single-epilogue restructure: every `pm_transpile_*` site emitting `MIR_RET` (early returns, the implicit `return None` trailer, exception-check bailouts).
- **P0c. Args census → PS5 decision.** Enumerate all `MIR_ALLOCA` arg-buffer sites (`transpile_py_mir.cpp:1411`, `:2248`, `:3661`, plus any others) with arity distribution. Decide between (a) a watermarked per-thread args region on the `js_args` pattern — preferred, ideally by making the existing js_args stack language-neutral rather than cloning it — and (b) static caller-frame reservation (per-site arity is compile-time known). Record the decision here.
- **P0d. Ownership census.** Every consumer that reads `fn->closure_env` raw (generator resume, `py_call_function`, class machinery); every other Item-bearing allocation from `py_input->pool` besides envs/frames (anything found joins the P2 migration list). Verify whether the `LMD_TYPE_FUNC` GC trace already follows Lambda `Function::closure_env` — if yes, P2b's frame-as-traced-env falls out of existing tracing.
- **P0e. Shared-primitive factoring prep.** Identify JS-specific dependencies inside `jm_begin_function_frame`/`jm_finish_function_frame` (exception TLS touches, JS context fields) so P1b can factor a language-neutral `em_begin/finish_function_frame` core into `mir_emitter_shared.hpp` with thin JS/Python wrappers.
- Exit: PO1 fixed and gated; censuses + PS5 decision recorded.

### Stage P1 — Frames, rooting, entry boundary

- **P1a. Single-epilogue restructure first, as its own commit** (no rooting yet — the phase-1 1c / phase-2 J1b pattern): all returns in a generated Python function branch to one epilogue label. Zero behavior change; full gates.
- **P1b. Promote the frame pair to the shared emitter.** Language-neutral `em_begin_function_frame`/`em_finish_function_frame` in `mir_emitter_shared.hpp`, composed from the existing `em_load/store_frame_top` primitives; JS's `jm_*` pair becomes a thin wrapper over it (mechanical — JS baselines gate this commit alone). This is the U21 dividend and rule-13 dedup applied to frames.
- **P1c. Python emits frames.** `PyMirTranspiler` calls the shared pair: checked prologue (root + number watermark save, static root-count reservation), epilogue restore. Rooting predicate per PS2: every Item-typed local, plus raw env/args pointers, gets a static root slot — refreshed on assignment and published before helper calls (the J1 discipline). Ints/bools/None/floats never root.
- **P1d. Entry boundary (PS9, arm-in-place).** `transpile_py_to_mir` arms the recovery point + snapshots both watermarks before invoking `py_main` (`lambda_side_stack_snapshot/restore`, the `transpile-mir.cpp:14732` pattern) and restores on the recovery branch; stack-overflow guard coverage verified for the `py` entry. Route-through-runner is deferred to unified-AST entry convergence — record that as the standing decision.
- **P1e.** Conservative scan stays on (SF9) — the correctness net for everything P2 hasn't migrated yet.
- Gates (§3), plus: a forced-GC stress fixture over `test/py/` closures/generators under ASan.

### Stage P2 — Ownership: envs and generator/coroutine frames

- **P2a. Envs.** `py_alloc_env` switches from `pool_calloc(py_input->pool, …)` to the traced env allocation (`GC_TYPE_JS_ENV`; rename to `GC_TYPE_GUEST_ENV` only if the enum + trace-dispatch touch stays trivial). Item half traced precisely; one raw scalar-tail slot per cell (SF18); capture and write-back re-home wide values into the tail. `scope_env_slot_count` is transpile-time static — sized exactly.
- **P2b. Generator/coroutine frames.** Per the P0d finding, prefer: the resume frame *is* a traced env-style object hung off `fn->closure_env`, so the existing `Function` trace path owns it (the JS J0b outcome — generator state as env residency, no separate SF20 record needed). Slot 0 (state word) lives in the raw tail or a native field, not the traced Item region.
- **P2c. Suspension barrier.** Suspend/resume re-homes number-stack residue into the frame's tail (SF20); Python's asyncio is a synchronous single-thread drive (`py_coro_drive`), so no cross-thread resume complications yet — but the invariant is enforced now so K-series doesn't reopen it.
- **P2d. Retire pool-pinning** for envs/frames; the `py_input` pool remains for what it legitimately owns (AST-adjacent and parse-lifetime data). Until this stage lands, the invariant gets a comment at `py_alloc_env`/`py_gen_create` declaring the pool dependency (PO2's interim ask).
- **P2e. Caps in this blast radius** (PO4 partial): generator frame ≤ 256 slots and `closure_field_count` ≤ 255 become checked, sized allocations — error loudly, never truncate.
- Gates, plus: env/generator churn profile shows reclamation (vs. monotonic pool growth today); GC-stress + ASan across `test/py/` generators/async/closures fixtures.

### Stage P3 — Numbers, args, re-homing surface

- **P3a. Args off `MIR_ALLOCA`** per the P0c decision; the kwargs arity cap (`Item new_args[17]`, `py_runtime.cpp:2017`) is resolved by the same mechanism (PO3 + PO4 partial).
- **P3b. Number lifetime live.** With P1 frames in place this is mostly consequence: verify returns reduce to the shared scalar lane before restore and rebuild in the caller extent (the J3 approach — no proto churn).
- **P3c. Exceptions + singletons.** `py_raise` gains the `js_throw_value` re-home (pointer-backed wide scalar → traced heap storage before the singleton outlives the raising frame); `py_exception_pending`/`py_exception_value`/`g_coro_return_value` become `__thread` (PO7 partial).
- **P3d. Retire helper-side heap doubles (PS10/PO5).** Migrate the ~6 `heap_alloc(LMD_TYPE_FLOAT)`/`lambda_float_ptr_to_item` sites (e.g. `py_to_float`, `py_runtime.cpp:106–125`) to `push_d` — helpers box into the calling frame's extent by SF6 construction. Add a float-identity regression (dict-key / `is` paths that could observe pointer vs. inline identity).
- Gates, plus: long-loop soak (steady-state number-stack watermark across a hot generator/arith loop); Python benchmarks under `test/benchmark/*/python/` within noise where runnable.

### Stage P4 — Cleanup, caps, docs

- **P4a. Remaining PO4 caps** get grow-or-error decisions: `py_module_vars` 1024 (silent drop today), `asyncio.gather` ≤ 6, `func_entries[128]`, `var_scopes[64]`, `loop_stack[32]`.
- **P4b. Rewrite `doc/dev/Python_Runtime.md`** (PO9) against the aligned architecture — once, after P1–P3, not incrementally.
- **P4c. Telemetry + closure.** `LAMBDA_MIR_LOG_FRAME_SLOTS` reports Python root counts / number-watermark use; design-doc statuses updated (`Lambda_Design_Stack_Frame_Python.md` → implemented; PO ledger items closed individually); memory sync.

---

## 3. Gates (every stage)

- `make test-jube` green (owns `test/py/` fixtures + goldens); `test/test_py_gtest.exe` all passing
- **Lambda baselines byte-identical** (`make test-lambda-baseline`) and **JS baselines unchanged** (`make node-baseline` no regression from current, JS gtest green) — the two-runtime protection contract
- P1+: `py` entry stack-overflow fail-fast behaves like the Lambda/JS entries (new coverage — this never existed for Python)
- P2/P3: GC-stress + ASan runs over `test/py/`; churn/soak profiles as listed per stage
- P0a onward: the int-overflow promotion fixture stays green permanently

## 4. Success criteria

Python-generated code runs with precise side-stack rooting and frame-scoped numbers through the same shared `em_*` frame pair as Lambda and JS; closure envs and generator/coroutine frames are GC-owned and reclaimed (the pool-pinning invariant deleted, not just documented); call args no longer use `MIR_ALLOCA`; the `py` entry has a real recovery boundary; int56 overflow promotes to BigInt; helper floats are inline — with Lambda and JS behavior byte-identical throughout, and Python no longer the blocker for SF9 conservative-scan retirement.

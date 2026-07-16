# Lambda Stack Frame — Python Runtime: Current State, Alignment Design, Open Issues

**Status:** design PROPOSED — alignment decisions PS1–PS10, open-issues ledger PO1–PO9; nothing implemented yet. **Implementation plan: `vibe/Lambda_Impl_Stack_Frame_Py.md`** (stages P0–P4, gates, censuses).
**Date:** 2026-07-15
**Context:** Third front-end onto the stack-frame architecture. `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20) is IMPLEMENTED for Lambda MIR-Direct and LambdaJS (phase records: `vibe/Lambda_Impl_Stack_Frame.md`, `vibe/Lambda_Impl_Stack_Frame_JS.md`, `vibe/Lambda_Impl_Stack_Frame_JS2.md`). This doc reviews where LambdaPy (`lambda/py/`, ~18.6k LOC across 17 files) stands, designs its alignment, and ledgers what stays open — the Python analog of the JS JO ledger (`vibe/Lambda_Design_Stack_Frame_JS.md`). Python is also the designated first guest port for the unified AST (U-series), so the alignment should ride the shared-emitter machinery rather than grow a third bespoke frame implementation.

Facts verified in code 2026-07-15; file:line references are to that tree state.

---

## 1. Current state, and its issues

### 1.1 The headline: Python never adopted *any* rooting generation

The Python transpiler predates both the old heap-root-frame machinery and the new side stacks, and emits **neither**:

- `transpile_py_mir.cpp` includes the shared emitter (`:13`) and routes reg/label/insn emission through `MirEmitter` (`pm_new_reg`→`em_new_reg` `:245`, etc.), but makes **zero** calls to frame primitives — no `em_load/store_frame_top`, no prologue/epilogue composition, no watermark save/restore, no root slots. (JS composes `jm_begin_function_frame`/`jm_finish_function_frame` on the same shared primitives.)
- **Zero** `heap_jit_gc_root_frame_*` calls — Python skipped the G1-era machinery entirely.
- **Zero** nursery vocabulary — Python also never used the (now-deleted) numeric nursery.

GC safety for JIT-live Python Items therefore rests entirely on the **conservative C-stack scan** (Items in MIR vregs → native registers/stack slots, found conservatively), plus 7 registered BSS singletons/tables, plus one structural accident: closure envs and generator frames live in the **`py_input` mempool**, which is never freed mid-execution — *pool residency, not the GC, is what keeps them alive* (§1.4).

### 1.2 Numbers

- **Ints** are inline int56 tagged Items (`pm_box_int_const`, `transpile_py_mir.cpp:415`; `PY_ITEM_INT_TAG` `:63`). No `push_l` anywhere (count 0). **BigInt** rides `LMD_TYPE_DECIMAL` via mpdecimal (`py_bigint.cpp:69,147–155`) — a GC heap object, consistent with N1–N9 and the JS BigInt row of OS1.
- **Floats**: the JIT boxing path is `pm_box_float` → `push_d` (`transpile_py_mir.cpp:424`) — Python gets SF-era inline doubles (+ side-number-stack residue for tiny/subnormals) *transitively*, as a consumer of the shared boxer. But it emits **no number watermarks**, so any residue slot allocated under `py_main` accumulates for the whole run (exec-base-frame semantics — the pre-J3 JS footing).
- **Legacy split**: ~6 runtime-helper sites still heap-box doubles via `heap_alloc(sizeof(double), LMD_TYPE_FLOAT)` + `lambda_float_ptr_to_item` (e.g. `py_to_float`, `py_runtime.cpp:106–125`) instead of `push_d` — so JIT-produced floats are inline while helper-produced floats are GC-heap objects. Two representations for one Python type.

### 1.3 The int-overflow landmine (independent of SF)

`pm_box_int_reg` (`transpile_py_mir.cpp:536–572`) range-checks against INT56 min/max, and its comment says "call py_add runtime for bigint promotion" — but the emitted fallback actually produces **`ITEM_ERROR`** (`:563–565`), not a BigInt. Inline arithmetic that overflows int56 yields an error Item instead of promoting; only the runtime-helper arithmetic path promotes correctly (`py_runtime.cpp:816`). A silent-wrong-answer path contradicting its own comment. → **PO1**.

### 1.4 Closures, generators, coroutines: pinned by pool, invisible to GC

- `py_alloc_env` (`py_runtime.cpp:1979–1982`) = `pool_calloc(py_input->pool, size*8)` — a raw `uint64_t*` slot array. **Not** a traced object, **not** a registered root range. Weaker than even the pre-J2 JS pattern (JS envs were at least root-registered; Python envs are simply invisible). Env slots hold boxed Items only; **no scalar-tail mechanism**.
- Generators/coroutines: `py_gen_create` (`py_runtime.cpp:2325–2337`) pool-allocates a `Function` plus a `uint64_t*` resume frame hung off `fn->closure_env` (slot 0 = state, slots 1..N = locals; `transpile_py_mir.cpp:6479`). **Fixed caps:** frame ≤ 256 slots, `closure_field_count` clamped to 255. Rooting: pool residency only, plus the lazily-registered coroutine side channel `g_coro_return_value` (`py_async.cpp:52–58`).
- The de-facto invariant — *"the `py_input` pool is never collected or reset mid-run"* — is load-bearing and undeclared. Any precise-GC end state, pool-reset change, or long-running embedding breaks it, and conservative-scan retirement (SF9's end state) would leave every env and suspended frame unrooted. → **PO2**.

### 1.5 Exceptions, module vars, args, containers

- **Exceptions**: pending-exception singleton + return-value checks, the `js_exception_value` pattern (`py_exception_pending`/`py_exception_value`, `py_runtime.cpp:33–34`; `py_raise` `:2201`). No longjmp. The singleton **is** GC-rooted (`:55`) but is file-static, **not `__thread`** (→ PO7), and has no wide-scalar re-home on raise (harmless today — no watermarks to escape — but required the moment PS-frames land).
- **Module vars**: `static Item py_module_vars[1024]`, root-registered as a range (`py_runtime.cpp:37–39,57`) — the OS1 globals row, correct as-is; cap is silent (→ PO4).
- **Call args**: every call site emits a fresh **`MIR_ALLOCA`** args buffer (`transpile_py_mir.cpp:1411–1425`, `:2248`, `:3661`). Two problems: the codebase deliberately avoids `MIR_ALLOCA` elsewhere ("MIR inlining ALLOCA bug on ARM64" — the exact landmine SF5 dodged), and alloca'd args are conservative-scan-protected only. Runtime kwargs path caps arity at ~16 (`Item new_args[17]`, `py_runtime.cpp:2017`). → **PO3**.
- **Containers**: lists/tuples = Lambda `Array`, dicts/objects = Lambda `Map` (`py_runtime.cpp:1340–1418`). **Zero `->extra` uses** in `lambda/py/` — no companion-pointer hack, no SF15-J analog needed. Container wide-scalar re-homing (SF15/SF16) arrives automatically through the shared `fn_*`/container helpers once frames exist.
- **Entry point**: Python **bypasses `runner.cpp`** — `main.cpp:2471` dispatches `py` straight to `transpile_py_to_mir`, which parses, JIT-compiles, and runs `py_main` in one call. None of the SF17 recovery boundaries (armed in the runner) cover this entry. → **PO6**.

### 1.6 The 7 registered roots (complete census)

| root | site | pins |
|---|---|---|
| `py_exception_value` | `py_runtime.cpp:55` | pending-exception Item |
| `py_stop_iteration_sentinel` | `py_runtime.cpp:56` | StopIteration sentinel |
| `py_module_vars[1024]` (range) | `py_runtime.cpp:57` | module/global table |
| `py_object_class` / `py_type_class` | `py_class.cpp:521–522` | builtin class Items |
| `py_builtin_class_items[]` (range) | `py_class.cpp:523` | builtin exception/class table |
| `g_coro_return_value` | `py_async.cpp:54` | coroutine return side channel |

All one-shot BSS registrations — legitimate globals-row entries. Everything else (envs, generator frames, container backing stores) relies on pool residency + conservative scan.

### 1.7 Issues table

| # | Issue |
|---|---|
| P-I1 | No frame emission of any kind: no precise roots, no number watermarks; the BUG-001-class under-retention hole (R-I5) is fully latent in Python. |
| P-I2 | Numbers accumulate for the whole run (no watermark restore) — exec-base-frame semantics, the pre-J3 footing. |
| P-I3 | Envs and generator frames are GC-invisible pool arrays; aliveness is an accident of pool lifetime (undeclared invariant). |
| P-I4 | Inline int56 overflow emits `ITEM_ERROR` instead of BigInt promotion (correctness bug, comment contradicts code). |
| P-I5 | Dual float representation: JIT inline vs. runtime-helper GC-heap doubles. |
| P-I6 | Per-call `MIR_ALLOCA` args — the documented ARM64 MIR landmine, avoided by both other transpilers. |
| P-I7 | Python entry bypasses all SF17 recovery boundaries. |
| P-I8 | Exception/coroutine singletons are file-static, not `__thread` — not isolate/K-series ready. |
| P-I9 | `doc/dev/Python_Runtime.md` is badly stale: claims ~7.8K LOC/9 files (now ~18.6K/17), "closures/classes not yet implemented" (both implemented), and nursery-based float allocation (nursery deleted). |

---

## 2. Alignment design (PS ledger)

The governing principle: Python is the **smallest** SF port of the three — no DateTime traffic, no `push_l` (ints are int56 or BigInt-as-GC-object), floats already inline via the shared `push_d`, containers already plain `Array`/`Map` with no `extra` collision. The port is essentially: frames + rooting, env/generator ownership, and boundary hygiene. It should reuse the JS phase's machinery wholesale.

**PS1 — Frame prologue/epilogue via the shared emitter; promote the JS pair rather than write a third.** `jm_begin_function_frame`/`jm_finish_function_frame` compose the shared `em_load/store_frame_top` primitives (`mir_emitter_shared.hpp:172–207`) into exactly what Python needs: checked side-stack prologue, static root-count reservation, watermark save, single-epilogue restore with scalar-lane extraction. Promote that pair (or its core) into `mir_emitter_shared.hpp` as language-neutral `em_begin/finish_function_frame` and have both JS and Python call it — the unified-AST Phase-0 direction (U21), and CLAUDE.md rule 13 applied to frames. Single-epilogue discipline: `pm_transpile_*` currently emits multiple raw `MIR_RET`s (implicit `return None` trailer, early returns); funnel through one epilogue label as J1b did.

**PS2 — Rooting predicate: everything is ANY, so (initially) every Item-typed local roots.** Python has no type inference — all values are boxed Items in `MIR_T_I64` regs. Honest typing therefore can't narrow the root set the way it did for Lambda/JS; the initial port roots every heap-capable local (strings, containers, functions, envs, BigInt/DECIMAL, exception values) — which, absent inference, is every local. Ints/bools/None/floats are inline and never root (SF2/SF11 hold trivially). The perf recovery path is PO8 (evidence-based inference via the unified AST), not a weaker rooting rule.

**PS3 — Envs become GC-traced objects with scalar tails (the J2 move).** `py_alloc_env` switches from `pool_calloc(py_input->pool, …)` to the `GC_TYPE_JS_ENV` allocation the collector already traces precisely (Item half traced, one raw scalar-tail slot per Item — SF18). The type is language-neutral in practice; rename to `GC_TYPE_GUEST_ENV` or reuse as-is. Envs then die with their closures; the pool-pinning invariant retires for envs. Slot count is transpile-time static (`scope_env_slot_count`) — same static-sizing property JS relied on.

**PS4 — Generator/coroutine frames become tail-bearing GC-owned records (the SF20 shape).** The resume frame moves from a raw pool array hung off `fn->closure_env` to a traced allocation: Item region traced, scalar tail never scanned, suspension-barrier invariant (nothing in a suspended frame points into any thread's side stacks — meaning resume/suspend re-homes number-stack-resident residue into the frame's tail, exactly SF20/J3's env-tail treatment). The `Function` object's trace hook follows the frame, as `JsFunction` traces its env. The ≤256-slot cap becomes a checked, sized allocation (PO4).

**PS5 — Args: replace per-call `MIR_ALLOCA`.** Three options, decided by a P0 call-site census: (a) a watermarked per-thread args region on the js_args pattern (registered once, SF19 downward-safe — preferred; possibly *the same* region if the js_args stack is made language-neutral); (b) caller-frame static reservation (args count is static per call site); (c) keep alloca. Option (c) is rejected on two grounds: the documented ARM64 `MIR_ALLOCA` inlining bug, and alloca args being conservative-scan-only — Python would permanently block SF9 scan retirement.

**PS6 — Exceptions: keep the singleton, add the two SF touches.** `py_raise` gains the `js_throw_value` re-home (a pointer-backed wide scalar moves to traced heap storage before the singleton outlives the raising frame's watermark); the singleton (and `g_coro_return_value`) become `__thread` for isolate readiness (PO7). Error propagation stays return-value + checks — already SF17-compatible.

**PS7 — Module vars: already correct.** The rooted 1024-slot BSS range is the OS1 globals row. Only the silent cap needs a failure-mode decision (PO4).

**PS8 — Containers: nothing to do.** Plain `Array`/`Map`; SF15/SF16 re-homing arrives through the shared helpers the moment PS1 frames exist. No props companion, no SF15-J analog. (Aside, not SF: tuple immutability is unenforced — tuples *are* lists internally.)

**PS9 — Entry/recovery boundary.** Either route `py` through the runner's execution path (preferred if the unified AST converges entry points anyway) or arm the same three SF17 boundaries at the Python entry: recovery point + both-watermark snapshot/restore (`lambda_side_stack_snapshot/restore`, the transpile-mir.cpp:14732 pattern), stack-overflow guard coverage for `py_main`, and heap-reset hygiene between REPL/batch runs.

**PS10 — Retire helper-side heap doubles.** Migrate the ~6 `heap_alloc(LMD_TYPE_FLOAT)`/`lambda_float_ptr_to_item` sites to `push_d` (helpers box into the calling frame's extent by SF6 construction). Closes the dual-representation split (P-I5) and removes Python's last GC-heap plain doubles — the same `box_float_cold`-retirement move JS made.

### Staging

Detailed stages, censuses, and gates live in the implementation plan, `vibe/Lambda_Impl_Stack_Frame_Py.md`: **P0** preludes (PO1 fix + audits) → **P1** frames/rooting/entry boundary (PS1, PS2, PS9) → **P2** env + generator ownership (PS3, PS4) → **P3** args/numbers/re-homing (PS5, PS6, PS10) → **P4** caps + docs. The standing contract: Lambda **and** JS baselines byte-identical at every stage; Python's own gate is `make test-jube` + `test_py_gtest`.

---

## 3. Open issues (PO ledger)

Same convention as the JS JO ledger: OPEN until individually decided/closed.

**PO1 — Inline int overflow emits `ITEM_ERROR` instead of promoting to BigInt.** `pm_box_int_reg`'s fallback (`transpile_py_mir.cpp:562–565`) contradicts its own comment; only runtime-helper arithmetic promotes (`py_runtime.cpp:816`). A correctness bug independent of SF — fix does not need to wait for the port: route the overflow branch to the runtime helper. Needs a regression fixture (e.g. `(2**55 - 1) + 1` through JIT-inlined `+`).

**PO2 — Pool residency is the de-facto rooting for envs and generator frames.** Undeclared, load-bearing, and incompatible with the SF end state: conservative-scan retirement or any `py_input` pool reset/free would leave them dangling. Resolved by PS3/PS4; until then it should be stated as an invariant comment at `py_alloc_env`/`py_gen_create`. Also leak-shaped: the pool only returns memory at teardown, so env/generator churn accumulates for the run (the nursery pattern, in pool form).

**PO3 — Per-call `MIR_ALLOCA` args.** The ARM64 MIR-inlining landmine both other transpilers deliberately avoid, plus per-call native-stack growth in hot loops, plus conservative-only protection. Resolved by PS5; the arity ~16 cap in `py_call_function_kw` (`Item new_args[17]`) rides the same fix.

**PO4 — Silent fixed caps.** Generator frame ≤ 256 slots / closure fields ≤ 255 (`py_runtime.cpp:2326,2334`), `asyncio.gather` ≤ 6 coroutines (`py_async.cpp:144–154`), call arity ~16, `py_module_vars` 1024, `func_entries[128]`, `var_scopes[64]`, `loop_stack[32]`. Same disease as the JS/Lambda "silent-cap" pattern — each needs a grow-or-error decision; none should truncate silently.

**PO5 — Dual float representation** (JIT inline vs. helper GC-heap doubles). Resolved by PS10; open until landed. Includes an identity wrinkle: heap-boxed doubles have pointer identity where inline doubles don't — any `is`-comparison or dict-key path that observes the difference is a latent behavioral inconsistency.

**PO6 — No recovery boundary at the Python entry.** `py` bypasses `runner.cpp` and none of the SF17 sites cover it: a SIGSEGV/stack-overflow during `py_main` has no armed recovery point, and once P1 lands, no watermark restore either. Resolved by PS9; the route-through-runner vs. arm-in-place decision should be taken with the unified-AST entry-point convergence in view.

**PO7 — Runtime singletons are file-static, not `__thread`.** `py_exception_pending`/`py_exception_value` (`py_runtime.cpp:33–34`) and `g_coro_return_value` (`py_async.cpp`) — unlike the JS TLS equivalents. Blocks K-series isolates / RC2 concurrent runtimes; also the whole `py_input`-pool global is per-process. Fold into the concurrency globals ledger (JT §6.5 pattern) when Python joins it.

**PO8 — No type inference: everything boxed, every local rooted, every operator a C call.** The Python transpiler has no evidence-based inference (JS has it; Lambda has honest typing), so PS2's root-everything predicate and call-per-operator codegen set the perf ceiling. The recovery path is the unified AST (Python is the designated first guest port): shared call-site inference at L3 would narrow both rooting and boxing. Not SF work — but SF's frame overhead is proportional to rooted-slot count, so inference is the multiplier on the whole port.

**PO9 — `doc/dev/Python_Runtime.md` needs a rewrite.** ~7.8K/9-file claims vs. ~18.6K/17 files; "closures — planned (not yet implemented)" and "class system — not yet implemented" while `py_class.cpp`/`py_async.cpp`/`py_bigint.cpp` ship and are tested; float rows still describe the deleted GC nursery (`Python_Runtime.md:106,137`). Rewrite after P1–P3 land so it documents the aligned architecture once, not twice.

### Ownership of the open issues

PO1–PO6 and PO9 are owned by the implementation plan's stages (PO1 → P0a; PO2 → P2; PO3 → P3a; PO4 → P2e/P3a/P4a; PO5 → P3d; PO6 → P1d; PO9 → P4b). Two remain design-level and are owned elsewhere: **PO7** (full isolate readiness) rides the concurrency globals ledger (JT §6.5 pattern), and **PO8** (type inference) rides the unified-AST guest port — SF frame overhead is proportional to rooted-slot count, so PO8 is the multiplier on the whole port.

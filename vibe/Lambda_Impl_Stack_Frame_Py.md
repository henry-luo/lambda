# Lambda Python Runtime — Implementation Plan (Stack Frame + Unified AST)

**Status:** draft v2
**Date:** 2026-07-20 (v1: 2026-07-15, stack-frame only)
**Supersedes/absorbs:** `vibe/Lambda_Impl_Unified_AST_Python.md` (2026-07-18) — its content is Track A below; that file has been deleted (recoverable from git history).
**Design authorities:**
- Track F (frames/rooting/ownership): `vibe/Lambda_Design_Stack_Frame_Python.md` (PS1–PS10, PO1–PO9), over `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20) and **`vibe/Lambda_Design_Stack_API.md` (implemented + verified 2026-07-20)** — the latter now defines the actual shared-emitter surface Python targets. Prior phase records: `vibe/Lambda_Impl_Stack_Frame.md`, `…_JS.md`, `…_JS2.md`.
- Track A (unified AST port): `vibe/Lambda_Design_Unified_AST.md` (U1–U26; esp. §2.3 catalog, §2.4 coverage, §7 guest formula, §8 Phase 6), `vibe/Lambda_Unified_AST_Impl_Plan.md` (main track), `doc/Lambda_Jube_Runtime.md`, `vibe/Lambda_Design_Concurrency.md` (K17 layers).

---

## 1. What changed in v2 — code verification, 2026-07-20

The Stack API (`Lambda_Design_Stack_API.md`) landed between v1 and now. Every claim below was re-verified against the working tree; v1/absorbed-doc items that verification invalidated are corrected in the stages.

**Python's actual position against the shared emitter** (the honest baseline for Track F):

- `PyMirTranspiler` embeds `MirEmitter em` by value (`transpile_py_mir.cpp:139`) but uses it as a **register/label/instruction/call convenience layer only**: 13 `em_*` helpers behind thin `pm_*` wrappers (`pm_new_reg`→`em_new_reg` at `:247`, `pm_call_0..5`/`pm_call_void_1..2` at `:265–309`, `em_import_cache_new` ×2), touching 4 of `MirEmitter`'s 18 fields (`ctx`, `func`, `func_item`, `import_cache`).
- **Every `pm_call_*` passes `include_signature = false`** (`:266` onward) — no `MirImportEntry` call signature (GC effect, `scalar_home_lane_mask`, `accepts_caller_scalar_home`) is ever recorded for a Python call.
- **All emitter callbacks are null** for Python: `before_may_gc_call`, `root_call_value`, `after_call_result`, `convert_rep`, `note_mir_call`, `call_owner`. These are the mechanism by which the shared call path records safepoints and root/adoption events.
- **No frame at all:** `em.frame.active` never set; no `root_base`/`number_base`; no `side_root_top`/`side_number_top` store/restore; zero uses of `em_finalize_frame_prologue`, `em_finalize_recorded_roots`/`em_finalize_semantic_root_write_back`, `em_finalize_scalar_homes`, `em_root_note_candidate`, `em_adopt_scalar_item`, `em_call_direct`. Conservative-scan-only.
- **12 return sites**, all `MIR_new_ret_insn` emitted inline (`:2848, 2858, 2912, 3586, 3604, 4717, 6615, 6661, 6760, 7017, 7029, 7276`) — no epilogue funnel. (Grepping `MIR_RET` finds 0; v1's census wording was wrong.)
- **8 `MIR_ALLOCA` arg-buffer sites** (`:1413, 1496, 1526, 1821, 1895, 2251, 2318, 3657`), not the 3 v1 cited. Feeding arrays `arg_regs[3]` (`:1496`) and `arg_regs[16]` (`:1526`, `:1821`) **silently truncate** via `while (arg && argc < N)`.
- **PO1 still live:** `pm_box_int_reg` overflow branch (`:560–568`) — comment says "call py_add runtime for bigint promotion", code emits `ITEM_ERROR`. Three call sites: `:959`, `:3154`, `:3451`.

**Shared-API facts that changed v1's assumptions:**

- **`em_begin_function_frame`/`em_finish_function_frame` do not exist** and were deliberately not built. The shipped surface is granular primitives (`em_frame_dispose/suspend/restore`, `em_finalize_frame_prologue`, `em_finalize_scalar_homes`, `em_finalize_function_metadata`, `em_adopt_scalar_item`, `em_store_frame_top`, root finalizers) plus one mutable `em.frame` (`MirFrameState`, `mir_emitter_shared.hpp:380–399`), composed per language:
  - JS: `jm_begin_function_frame`/`jm_finish_function_frame` (`js_mir_hashmap_scope_utils.cpp:345,404`); `jm_emit` intercepts `MIR_RET` while `frame.active` into a jump to `frame.return_label` (`:465–470`); roots via `em_finalize_semantic_root_write_back`.
  - Lambda: `begin_function_epilogue`/`emit_function_return`/`finalize_side_root_frame`/`finalize_gc_root_publication` (`transpile-mir.cpp:541, 548, 649, 678`); roots via `em_finalize_recorded_roots`; plus a return-lane concept (`RETURN_LANE_SCALAR/ERROR` → `Context::mir_return_lane`) JS doesn't have.
  - The genuinely common finish spine both share: `em_finalize_scalar_homes` → `em_finalize_frame_prologue` (same five `offsetof(Context, side_*)` operands) → `em_finalize_function_metadata` → `em_frame_dispose`.
- **`lambda_side_stack_snapshot/restore` do not exist.** The entry-boundary mechanism is `LambdaRecoveryCheckpoint` + `sigsetjmp`: `lambda_recovery_checkpoint_capture/restore/disarm` (`transpile-mir.cpp:14708–14726`; JS pattern at `js_mir_entrypoints_require.cpp:976–996`). `py_main` is invoked bare at `transpile_py_mir.cpp:7430` and `:7582`.
- **No shared args region exists** — `mir_emitter_shared.hpp` has no argument-stack concept. js_args is JS-private across three layers: runtime (`js_args_push/save/restore`, `js_runtime_function.cpp:125,146,153`; `js_runtime.h:232–234`), transpiler scope (`JsMirArgStackScope`, `js_mir_context.hpp:317–320`; `jm_begin_arg_stack_scope`, `js_mir_expression_lowering.cpp:13261`), and completion unwind (`js_mir_completion.cpp:157–173`).
- **P2a gets cheaper:** `GC_TYPE_JS_ENV` already allocates the SF18 layout — `size * 2` (Item half + raw scalar tail) at `lambda-mem.cpp:604`, traced via the `Function` path (`js_runtime_function.cpp:353`). Python's `py_alloc_env` (`py_runtime.cpp:1979–1982`) is `pool_calloc(py_input->pool, size * …)` with the *single-width* layout — so env migration is a **layout change**, not the trivial enum rename v1's parenthetical assumed. `GC_TYPE_GUEST_ENV` does not exist.
- **Scalar-home/hidden-ABI machinery is live** for Lambda+JS (`MirScalarReturnMode`, `incoming_scalar_home`, colored homes, discard scratch, `lambda_item_heap_rehome`); Python does not participate at all. Consequence: `push_d`-produced floats are activation-owned payloads — Track F's float migration (F3d) is **gated** on frames + adoption existing first.

**Verified still true (unchanged from v1 / absorbed doc):** the six `heap_alloc(LMD_TYPE_FLOAT)`+`lambda_float_ptr_to_item` sites (`py_runtime.cpp:106, 111, 123, 305, 472, 716`); non-`__thread` statics `py_exception_pending`/`py_exception_value` (`py_runtime.cpp:33–34`) and `g_coro_return_value` (`py_async.cpp:48`); all caps — `PY_MODULE_VAR_MAX 1024` (`py_runtime.cpp:37`), `py_asyncio_gather` ≤ 6 (`py_async.cpp:144`), `func_entries[128]` (`:153`), `var_scopes[64]` (`:145`), `loop_stack[32]` (`:149`), `Item new_args[17]` ×3 (`py_runtime.cpp:2017, 2100, 2114`) — plus previously undocumented ones: `lambda_entries[64]` (`:167`), `try_handler_labels[16]` (`:180`), `gen_yield_labels[32]` (`:204`), `gen_local_names[32][128]` (`:205`), `global_var_names[64][128]` (`:157`).

**Corrections to the absorbed unified-AST doc:**

- `mir_emitter_shared.hpp` is now **2,982 lines** (was 698 when measured 2026-07-18) — the Stack API added the entire frame/rooting/scalar-home subsystem. The "Phase 0 done" claim is now much stronger: the shared spine Python targets includes frames, not just reg/label/call primitives.
- `ast-core.hpp` is 815 lines; `lang_profile_for_name` is at `:809`.
- The `AST_NODE_START`/`AST_NODE_EVENT_HANDLER` = 541 collision is **already fixed** (`EVENT_HANDLER = 542` with root-cause comment, `ast-core.hpp:132–135`) — dropped from the amendment list.
- Corpus is now **41 scripts / 25 goldens / 14 orphaned `test_py_*` scripts** (same orphan list; `test_py_basic/builtins_v2/closures/comprehensions/defaults/dict_methods_v2/exceptions/extended/formatting/pkg_simple/print_kwargs/slicing/sort_key/string_methods_v2`).
- `transpile_py_mir.cpp` is 7,644 lines; `py_ast.hpp` 545; `build_py_ast.cpp` 2,429; `py_scope.cpp` 252.
- Nothing from that plan has been implemented: `PyMirVarEntry`/`PyModuleConstEntry`/`PyFuncCollected`/`PyScope` all present; `py_ast.hpp` still composition-based (`PyAstNode base;`); no Py kind range, no `OPERATOR_PY_MATMUL`, no `SCOPE_KIND_CLASS/COMPREHENSION`; TS sentinel still 2000 (`js_ast.hpp:134`).

---

## 2. Two tracks and their interlock

This doc now carries both Python upgrade programs, because they edit the same file and gate on the same corpus:

- **Track F — stack frames, rooting, ownership** (stages F0–F4; v1's P0–P4 renamed): make Python-generated code a well-behaved citizen of the SF/rooting architecture. Reuses the JS phase's machinery, which after the Stack API is *shared emitter machinery*.
- **Track A — unified AST port** (stages A0–A6; the absorbed doc's P0–P6 renamed): retarget `lambda/py/` onto the unified AST and compiler spine per `Lambda_Design_Unified_AST.md` Phase 6. Decision points PY1–PY10 keep their names (§12).

**Interlock and sequencing:**

1. **F0a (PO1 int-overflow fix) lands first, standalone** — it is a live correctness bug independent of both tracks.
2. **Gate zero (shared):** restore the 14 orphaned goldens before either track's first behavior-affecting stage (§11). Closures/exceptions/comprehensions are orphaned today, and both tracks need them in the gate.
3. **Track F is the near-term program.** It reuses machinery that exists now, closes the BUG-001-class rooting hole, and does not depend on the main unified-AST track. Track A's A0–A4 (substrate, kinds, structs, binding, analysis) are mechanical, start-now, and can interleave; A5–A6 (shared driver, modules) trail the main track's Phase 4/5 and land later.
4. **The F1b frame-composition decision is also an A5 down-payment.** Python becomes the *third* per-language frame composer; CLAUDE.md rule 13 says extract the shared shape at the third variant. The extraction target is exactly what Track A's shared lowering driver wants to own. F1b therefore extracts the common finish spine now (parameterized where Lambda/JS genuinely differ), and A5 later absorbs it into the driver.
5. **Frames survive the A5 collapse.** Track F invests in *what the generated code does* (frame prologue/epilogue, rooting events, scalar homes) expressed through shared `em_*` machinery; when A5 moves lowering into the shared driver, that emission moves with it unchanged. The F-track is not migrated twice.
6. **PO8 (type inference) is Track A's A4** (`resolve_param_evidence`); until then PS2's root-everything predicate is the Track F stance.
7. **Generators:** F2b/F2c fix *ownership* of today's generator frames (traced env off `fn->closure_env`) and must not wait; Track A defers *re-lowering* generators to the shared resumable transform (PY6). The ownership design survives the re-lowering because both hang state off the Function object.
8. **Blast radius:** Python compiles only into the jube variant (`lambda-jube.exe`; `build_lambda_config.json` target `jube`) — Python-side churn cannot break `make build`/`make test-lambda-baseline` except through shared-header edits, which carry the full three-suite gate.

---

## 3. Track F — frames, rooting, ownership (F0–F4)

Python is the smallest SF port of the three front-ends (design doc §2): no DateTime traffic, no `push_l`, floats already inline via the shared `push_d`, containers plain `Array`/`Map` with zero `extra` uses. The work is: frames + rooting, env/generator ownership, args off `MIR_ALLOCA`, and boundary hygiene — reusing the shared emitter's frame subsystem rather than building a third implementation.

**Track-F contract** (mirrors phases 1/2): Lambda **and** JS baselines byte-identical at every stage — two implemented runtimes are protected, not one. Python's own gate is `make test-jube` (owns `test/py/`) plus `test/test_py_gtest.exe`.

**Payoffs, in order of value:**
1. **Precise rooting for Python locals** — closes the latent BUG-001-class under-retention hole (P-I1); today Python is conservative-scan-only.
2. **Env/generator ownership** — retires the undeclared "py_input pool is never reset" invariant (PO2) that currently *is* the rooting; unblocks eventual SF9 conservative-scan retirement (Python is otherwise a permanent blocker).
3. **Args off `MIR_ALLOCA`** — retires the documented ARM64 MIR-inlining landmine (PO3) and per-call native-stack growth.
4. **Frame-scoped numbers + recovery boundary** — per-run numeric accumulation ends; `py_main` gets SF17 coverage it has never had.

### Stage F0 — Preludes and audit

- **F0a. Fix PO1 (standalone, lands first).** Reroute `pm_box_int_reg`'s overflow branch (`transpile_py_mir.cpp:560–568`, emitting `ITEM_ERROR` despite its own comment) to the runtime promotion path (the helpers already promote). Covers all three call sites (`:959`, `:3154` — including the `:3451` loop-counter box). Root-cause comment at the fix point (rule 12). New fixture + golden (rule 8): `test/py/test_py_int_overflow.py` exercising JIT-inlined `+`/`-`/`*` across the int56 boundary in both directions (e.g. `(2**54) * 4`, `-(2**55) - 1`) and verifying BigInt results, not errors.
- **F0b. Return-site funnel prep.** Census done (§1): 12 `MIR_new_ret_insn` sites. F1a funnels them; no further audit needed.
- **F0c. Args decision (PS5).** Census done (§1): 8 `MIR_ALLOCA` sites + silent `arg_regs[3]`/`[16]` truncation. v1's preferred option — "make the existing js_args stack language-neutral" — is now known to be a three-layer hoist (runtime globals + transpiler scope + completion unwind), a sub-project in itself. Decide between: (a) hoist js_args into a shared region (clean, larger); (b) static caller-frame reservation — per-site arity is compile-time known at 7 of 8 sites, and F1's frames give a natural home; (c) keep `MIR_ALLOCA` at the one truly dynamic site (`:1413`) initially. **Leaning (b) with (a) as the A5-era convergence.** The truncating `arg_regs` caps become checked errors in the same change (PO4 partial). Record the decision here. **[PF2]**
- **F0d. Ownership census.** Every consumer that reads `fn->closure_env` raw (generator resume, `py_call_function`, class machinery); every other Item-bearing allocation from `py_input->pool` besides envs/frames (anything found joins the F2 migration list). Verify whether the `LMD_TYPE_FUNC` GC trace follows `Function::closure_env` for Python functions the way it does for JS (`js_runtime_function.cpp:353` guards on `GC_TYPE_JS_ENV`) — if yes, F2b's frame-as-traced-env falls out of existing tracing once envs carry the GC header.
- **F0e. Frame-composition decision (replaces v1's P1b extraction premise).** The shared frame pair v1 wanted to promote was deliberately not built (§1); JS and Lambda hand-compose from granular primitives and differ in root finalizer (`semantic_root_write_back` vs `recorded_roots`) and return lanes. Decide the Python composition shape: **recommended** — extract the common *finish spine* (`em_finalize_scalar_homes` → `em_finalize_frame_prologue` → `em_finalize_function_metadata` → `em_frame_dispose`, plus the epilogue-funnel pattern) into a small shared helper parameterized by root-finalizer choice and lane handling, making JS/Lambda its first two callers and Python the third (rule 13); begin-side stays per-language field population for now. Full begin/finish unification rides A5. **[PF1]**
- Exit: PO1 fixed and gated; PF1/PF2 decisions recorded.

### Stage F1 — Frames, rooting, entry boundary

- **F1a. Single-epilogue restructure first, as its own commit** (no rooting yet — the phase-1 1c / phase-2 J1b pattern): all 12 return sites branch to one epilogue label, either via the JS `frame.active` RET-interception pattern in `pm_emit` or an explicit funnel. Zero behavior change; full gates.
- **F1b. Call-path signature + callback enablement** (new in v2 — prerequisite for everything below):
  - flip `pm_call_*` to `include_signature = true` so `MirImportEntry` signatures (GC effect, scalar-home lane masks) are recorded for Python calls;
  - install the emitter callbacks (`before_may_gc_call`, `root_call_value`, `after_call_result`, `note_mir_call`, `call_owner`; `convert_rep` when representation work starts) so the shared call path records safepoints and root events for Python exactly as it does for Lambda/JS;
  - audit `sys_func_registry` metadata for the `py_*` helper set (GC/re-entry/exception classes) — fail-closed conservative where unaudited.
- **F1c. Python emits frames.** Per PF1: populate `em.frame` (checked prologue — root + number watermark save, static root-count reservation via `em_finalize_frame_prologue`), epilogue restore via the shared finish spine. Root candidates via `em_root_note_candidate`; rooting predicate per PS2: every Item-typed local, plus raw env/args pointers, gets a static root slot — published through the shared finalizer (choose `em_finalize_recorded_roots` vs `em_finalize_semantic_root_write_back` per PF1's parameterization; Lambda's recorded-roots variant is the closer fit for Python's untyped-local model). Ints/bools/None never root; floats are self-tagged inline post-double-boxing-v3 (only the subnormal residue is pointer-backed).
- **F1d. Entry boundary (PS9, arm-in-place).** `transpile_py_to_mir` arms a `LambdaRecoveryCheckpoint` (capture/restore/disarm) around both bare `py_main` invocations (`transpile_py_mir.cpp:7430`, `:7582`), copying the JS entrypoint pattern (`js_mir_entrypoints_require.cpp:976–996`); stack-overflow guard coverage verified for the `py` entry. Route-through-runner is deferred to unified-AST entry convergence (A5/A6) — recorded as the standing decision.
- **F1e.** Conservative scan stays on (SF9) — the correctness net for everything F2 hasn't migrated yet.
- Gates (§11), plus: Python joins `make test-gc-rooting` as its fifth lane (forced-GC over `test/py/` closures/generators, under ASan) rather than a bespoke stress fixture.

### Stage F2 — Ownership: envs and generator/coroutine frames

- **F2a. Envs.** `py_alloc_env` switches from `pool_calloc(py_input->pool, size * sizeof(uint64_t))` to the traced env allocation — **adopting the existing `GC_TYPE_JS_ENV` `size * 2` layout** (Item half traced precisely, one raw scalar-tail slot per cell per SF18; `lambda-mem.cpp:604`). This is a layout migration at the three emission sites (`:2724, :4383, :6925`) and every env-slot indexing site, not an allocation swap alone. Rename to `GC_TYPE_GUEST_ENV` only if the enum + trace-dispatch touch stays trivial **[PF3]**. Capture and write-back re-home wide values into the tail. `scope_env_slot_count` is transpile-time static — sized exactly.
- **F2b. Generator/coroutine frames.** Per the F0d finding, prefer: the resume frame *is* a traced env-style object hung off `fn->closure_env`, so the existing `Function` trace path owns it (the JS J0b outcome — generator state as env residency, no separate SF20 record needed). Slot 0 (state word) lives in the raw tail or a native field, not the traced Item region. Survives Track A's later generator re-lowering (interlock §2.7).
- **F2c. Suspension barrier.** Suspend/resume re-homes number-stack residue into the frame's tail (SF20); Python's asyncio is a synchronous single-thread drive (`py_coro_drive`), so no cross-thread resume complications yet — but the invariant is enforced now so K-series doesn't reopen it.
- **F2d. Retire pool-pinning** for envs/frames; the `py_input` pool remains for what it legitimately owns (AST-adjacent and parse-lifetime data). Until this stage lands, the invariant gets a comment at `py_alloc_env`/`py_gen_create` declaring the pool dependency (PO2's interim ask).
- **F2e. Caps in this blast radius** (PO4 partial): generator frame ≤ 256 slots and `closure_field_count` ≤ 255 become checked, sized allocations — error loudly, never truncate. Include the newly cataloged `gen_yield_labels[32]`/`gen_local_names[32][128]` caps.
- Gates, plus: env/generator churn profile shows reclamation (vs. monotonic pool growth today); GC-stress + ASan across `test/py/` generators/async/closures fixtures.

### Stage F3 — Numbers, args, re-homing surface

- **F3a. Args off `MIR_ALLOCA`** per the PF2 decision; the kwargs arity cap (`Item new_args[17]` ×3, `py_runtime.cpp:2017/2100/2114`) is resolved by the same mechanism (PO3 + PO4 partial).
- **F3b. Number lifetime + scalar homes live.** With F1 frames and signatures in place, Python opts into the scalar-home contract: colored activation homes for pointer-backed `INT64` results (the only wide class Python traffics in — no DTIME, floats inline), `em_adopt_scalar_item` on returns crossing the epilogue, callee watermark fully restored. Verify with the million-iteration probes from the Stack API gate set.
- **F3c. Exceptions + singletons.** `py_raise` gains the `js_throw_value`-style re-home (pointer-backed wide scalar → traced heap storage before the singleton outlives the raising frame); `py_exception_pending`/`py_exception_value` (`py_runtime.cpp:33–34`) and `g_coro_return_value` (`py_async.cpp:48`) become `__thread` (PO7 partial).
- **F3d. Retire helper-side heap doubles (PS10/PO5).** Migrate the 6 `heap_alloc(LMD_TYPE_FLOAT)`/`lambda_float_ptr_to_item` sites (`py_runtime.cpp:106, 111, 123, 305, 472, 716`) to `push_d`. **Gated on F1a + F3b:** `push_d` results are activation-owned number-stack payloads; without the epilogue funnel and adoption contract, a subnormal/tiny boxed float returned across a frame boundary dangles. Exposure is small post-double-boxing-v3 but not zero. Add a float-identity regression (dict-key / `is` paths that could observe pointer vs. inline identity).
- Gates, plus: long-loop soak (steady-state number-stack watermark across a hot generator/arith loop); Python benchmarks under `test/benchmark/*/python/` within noise where runnable.

### Stage F4 — Cleanup, caps, docs

- **F4a. Remaining PO4 caps** get grow-or-error decisions: `py_module_vars` 1024 (silent drop today), `asyncio.gather` ≤ 6, `func_entries[128]`, `var_scopes[64]`, `loop_stack[32]`, `lambda_entries[64]`, `try_handler_labels[16]`, `global_var_names[64][128]`. (Note: `var_scopes`/`global_var_names` may be retired outright by Track A's A0 substrate work — coordinate rather than build growth twice.)
- **F4b. Rewrite `doc/dev/Python_Runtime.md`** (PO9) against the aligned architecture — once, after F1–F3 (and folding in whatever Track A stages have landed), not incrementally.
- **F4c. Telemetry + closure.** `LAMBDA_MIR_LOG_FRAME_SLOTS` reports Python root counts / number-watermark use via the now-populated `MirFunctionMetadata`; design-doc statuses updated (`Lambda_Design_Stack_Frame_Python.md` → implemented; PO ledger items closed individually); memory sync.

### Track F decision points

| # | Decision | Recommendation |
|---|---|---|
| **PF1** | Frame composition shape: extract common finish spine now vs. Python mirrors JS's hand-composition vs. full begin/finish unification | extract the finish spine (rule 13 — third composer), parameterized by root-finalizer + lanes; begin-side per-language; full unification rides A5 |
| **PF2** | Args mechanism: hoist js_args to shared vs. static caller-frame reservation vs. hybrid | static reservation (b) now — 7 of 8 sites have compile-time arity; shared-region convergence at A5 |
| **PF3** | Env GC type: reuse `GC_TYPE_JS_ENV` vs. rename `GC_TYPE_GUEST_ENV` | reuse unless the rename touch (enum + `js_runtime_function.cpp:353` guard) stays a two-line diff |

---

## 4. Track A — unified AST port (A0–A6)

*(Absorbed from `Lambda_Impl_Unified_AST_Python.md` rev 1, 2026-07-18; measurements and stale items corrected per §1. Decision points PY1–PY10 in §12 pending user confirmation, unchanged.)*

### 4.1 Goal and scope

Retarget the Python guest runtime (`lambda/py/`) onto the unified AST and compiler spine, per Phase 6 of `Lambda_Design_Unified_AST.md`. Python is the **first guest port and the acceptance test of the whole unified-AST design** — the empirical check on the ≥80%-core-coverage target (§2.4) and on the claim that a guest language collapses to *grammar + builder + LangProfile + runtime library*.

| Component | Today | Target |
|---|---|---|
| Grammar | `lambda/tree-sitter-python/` | **kept** as-is |
| AST | own `PyAstNodeType` enum + ~45 `Py*Node` structs (`py_ast.hpp`, 545 lines) | **replaced**: core nodes + a small Python range (2000–2499) in `ast-core.hpp` |
| Builder | `build_py_ast.cpp` (2,429 lines) → PyAst | **kept, retargeted** to emit common AST + bind on shared `NameScope` |
| Scope/binding | `py_scope.cpp` `PyScope` (own struct, shared `NameEntry`) | **replaced** by shared `NameScope` + `ScopeKind` extensions |
| Analysis | `PyFuncCollected` side tables inside the transpiler | **replaced** by shared `FnAnalysis` + `PyFnExt` |
| Lowering | `transpile_py_mir.cpp` (**7,644 lines**) | **collapse target**: shared driver + `PyProfile` handlers (~1,000–1,500 lines projected) |
| Runtime library | `py_runtime` / `py_builtins` / `py_stdlib` / `py_class` / `py_bigint` / `py_print` / `py_async` (~7,100 lines) | **kept** — the Item-level helpers *are* Python's dynamic semantics; profile-emitted code calls them |
| Tests | `test/py/` — 41 scripts, 25 golden pairs, `test_py_gtest.cpp` harness | **kept**; the per-stage green gate (plus additions, §11) |

Non-goals: no change to the J2 interop contract (cross-language calls still cross at the Item/C-ABI seam); no change to Python surface semantics observable in the test corpus; no port of Ruby/Bash here (they follow the pattern this port proves).

### 4.2 Current state audit (re-verified 2026-07-20)

**Shared infrastructure in place:**

- **`lambda/ast-core.hpp`** (815 lines): one `AstNodeType` space with core kinds blocked by level, Lambda range at 500+, JS-specific at 1000+, `JS_AST_NODE_TS_EXTENSION_SENTINEL = 2000` (`js_ast.hpp:134`). JS fully aliased onto it (`typedef AstNode JsAstNode`) — main-plan Phase 1 and much of Phase 2 are **done** for Lambda+JS. ~~541 collision~~ fixed (`EVENT_HANDLER = 542`).
- **Superset `Operator` enum** (Lambda ops + `OPERATOR_JS_*`); `typedef Operator JsOperator`.
- **Shared `NameEntry`/`NameScope`** with `ScopeKind {GLOBAL, MODULE, FUNCTION, BLOCK}` and the Lambda∪JS flag superset.
- **Shared core leaf structs**: `AstLiteralNode`, `AstIdentNode`, `AstUnaryNode`/`AstBinaryNode`, `AstCallNode`, `AstFieldNode`, `AstIfNode`, `AstMatchNode`/`AstMatchArm`, `AstTryNode`/`AstCatchNode` (single-arm), `AstFuncNode`/`AstMethodNode`, `AstClassNode`, L3 pattern nodes, `AstYieldNode`/`AstAwaitNode`, module nodes, `FnAnalysis`/`FnCapture`/`FnParamEvidence`, and a dormant `LangProfile` (`lang_profile_for_name` at `ast-core.hpp:809` knows only "lambda"/"js" — Python modules silently get `lambda_profile`).
- **`lambda/mir_emitter_shared.hpp`** (2,982 lines): the full Stack API surface — `MirEmitter`, unified `VarEntry` (typedef'd to `MirVarEntry`/`JsMirVarEntry`), `em_var_scope_*` machinery, call emission with normalized import metadata, **frames, rooting finalizers, scalar homes** (§1). Main-plan Phase 0 is done-plus for Lambda+JS.

**Python's position:** already aligned — `PyAstNode` base layout-identical to `AstNode` (composition via `base` member, `py_ast.hpp:156`); `py_scope.cpp` stores shared `NameEntry`; `PyMirTranspiler` embeds `MirEmitter` with `pm_*`→`em_*` wrappers; Python modules register in `module_registry` with `source_lang = "python"` (`transpile_py_mir.cpp:7575`). Still fully parallel (the actual work):

| Parallel piece | Where | Unified replacement |
|---|---|---|
| `PyAstNodeType` (~60 kinds), `PyOperator` (~44 ops), `PyLiteralType` | `py_ast.hpp:16–153` | core kinds + Py range 2000–2499; superset `Operator` |
| ~45 `Py*Node` structs with `base` member | `py_ast.hpp:156–545` | core leaf structs (inheritance) + Py-range residue structs |
| `PyScope`/`PyScopeType`/`PyVarKind` | `py_transpiler.hpp:14–39`, `py_scope.cpp` | shared `NameScope` + `ScopeKind` (needs CLASS/COMPREHENSION, §4.6) |
| `PyMirVarEntry` + `py_var_scope` hashmaps + `var_scopes[64]` | `transpile_py_mir.cpp:75–82, 145–146, 213–217` | shared `VarEntry` + `em_var_scope_*` |
| `PyFuncCollected` (captures, nonlocal/global lists, *args/**kwargs, generator/async flags, class info) + `PyLambdaCollected` | `transpile_py_mir.cpp:89–134` | `FnAnalysis` + `FnCapture` + `PyFnExt` via `FnExt` union |
| `PyModuleConstEntry` `module_consts` | `transpile_py_mir.cpp:225–231` | emitter const-pool API + unified module-var BSS |
| The entire lowering walk (expressions/statements/functions/classes/match/imports/comprehensions/TCO/generators) | `transpile_py_mir.cpp` | shared driver + `PyProfile` hooks; generators later ride the shared resumable-function transform (K17 layer 1) |
| `lang_profile_for_name` fallback to `lambda_profile` for "python" | `ast-core.hpp:809` | add `py_profile`, resolve `"python"` |

**Entry points and build wiring:** standalone `lambda-jube.exe py script.py` → `transpile_py_to_mir` (`transpile_py_mir.cpp:7288`); module path `load_py_module` (`:7470`, used from `build_ast.cpp` and Py↔Py imports at `:4123, :4142, :4285`). Python compiles only into the jube variant. Tests: `test_py_gtest.cpp` discovers `test/py/test_py_*.py` with golden `.txt`; **14 of 41 scripts have no golden and are silently skipped** (list in §1); one explicit skip (`test_py_import`, filesystem-import Bus error).

### 4.3 Target architecture

```
source.py ──tree-sitter-python──► CST
        ──build_py_ast (kept, retargeted)──► common AST (core + Py-range nodes)
                                             + scopes bound on shared NameScope
        ──shared passes:  capture analysis → FnAnalysis
                          evidence inference → ParamEvidence
        ──PyProfile hooks: validate, resolve_param_evidence (int→int64/BigInt)
        ──shared lowering driver (core control flow, calls, closures/scope-env,
                          destructuring engine, TCO, resumable transform)
          + PyProfile lowering handlers (operator/truthiness/member/call/error
                          semantics → py_* runtime helpers; Py-range ext nodes)
        ──MirEmitter (incl. Track F frames/rooting)──► MIR ──► JIT
```

The `PyProfile` hook table (handlers mostly *emit calls into the existing runtime library* — which is why the runtime stays and the transpiler collapses):

| Hook | Python policy | Runtime surface it emits |
|---|---|---|
| `lower_binary` / `lower_unary` | native `int64`/`float` fast path when both operand types proven (preserving `pm_transpile_as_native_int/float`); else helper call | `py_add/…/py_floor_divide/py_power`, bigint promotion inside helpers |
| `emit_truthy_test` | Python truthiness (empty containers falsy, `0`/`""`/`None` falsy) | `py_to_bool` / inline tag tests |
| `emit_to_number` | Python numeric coercion | `py_to_int`/`py_to_float` |
| `emit_member_get/set` | attribute protocol incl. descriptors, bound methods | `py_getattr`/`py_setattr`/`py_delattr` |
| `emit_call` | positional/default/`*args`/`**kwargs` arity policy; class-vs-function callee dispatch | direct MIR call, `py_call_*` shims, `py_class` construction |
| `emit_error_check` | try-context dispatch to nearest handler label; error-values per J3 | raise/except helpers |
| `resolve_param_evidence` | int evidence → `LMD_TYPE_INT64` (int64 fast path + runtime overflow promotion); float → FLOAT; container/string/reassigned → ANY | — |
| `lower_clause` | comprehension `CLAUSE_FOR`/`CLAUSE_WHERE`; decorator clauses | closure + iterator emission |
| `lower_ext_node` | Py-range residue: slice, with, del, assert, chained compare, class patterns, scope decls | `py_slice_*`, `__enter__/__exit__`, `py_match_*` |
| `validate` | Python static rules (`return` outside function, `nonlocal` with no binding, duplicate params, `await` outside async) | — |

### 4.4 Node and operator mapping catalog

Disposition legend: "core" = existing ast-core kind; "core+" = core kind needing a field/struct amendment (§4.5); "clause" = tier-2 clause node; "Py-range" = new kind in 2000–2499; "builder-norm" = normalized away by the builder.

**Expressions (L1):**

| Python node | Disposition | Target + notes |
|---|---|---|
| `IDENTIFIER` | core | `AST_NODE_IDENT` |
| `LITERAL` | core+ | `AST_NODE_LITERAL`; needs `int64_t` member + int/float `LiteralKind` (today: `double number_value` only; int64 must not round-trip through double). `is_bigint`/`bigint_str` already present |
| `FSTRING`, `FSTRING_EXPR` | core+ | promote JS `TEMPLATE_LITERAL`/`TEMPLATE_ELEMENT` (1002/1003) to core `AST_NODE_INTERP_STR` per U16; `format_spec` rides as *(var.)* field |
| `BINARY_OP` | core | `AST_NODE_BINARY` + superset `Operator` |
| `UNARY_OP`, `NOT` | core | `AST_NODE_UNARY` |
| `BOOLEAN_OP` | core | `AST_NODE_BINARY` `OPERATOR_AND/OR`; value-propagation short-circuit is profile lowering |
| `COMPARE` (chained) | split | single → `AST_NODE_BINARY` (builder-norm); chains stay `PY_AST_NODE_COMPARE_CHAIN` (desugar = U19 double-eval hazard) |
| `CALL` | core | `AST_NODE_CALL_EXPR`; kwargs → `AST_NODE_NAMED_ARG` promoted from Lambda range to core |
| `ATTRIBUTE` / `SUBSCRIPT` | core | `AST_NODE_MEMBER_EXPR` / `AST_NODE_INDEX_EXPR` (`AstFieldNode`, `computed`) |
| `SLICE` | core+ | new core `AST_NODE_RANGE` `{start; end; step; inclusive}` (Lambda `to` = second client) |
| `LIST` / `DICT`+`PAIR` | core | `AST_NODE_ARRAY` / `AST_NODE_MAP`+`PROPERTY` |
| `TUPLE` | core+ | `AST_NODE_SEQ` + `SeqKind {list, tuple, comma}` per U16 |
| `SET` | Py-range | `PY_AST_NODE_SET`; lowers to `py_set_new` |
| comprehensions, generator exprs | core+ | `AST_NODE_FOR_EXPR` promoted from Lambda range + core `CLAUSE_FOR`/`CLAUSE_WHERE`; result-kind *(var.)* `{list, dict, set, generator}`; generator exprs builder-norm to `AST_FUNC{is_generator}` closures (PY5) |
| `CONDITIONAL_EXPR` | core | `AST_NODE_IF_EXPR` value-position |
| `LAMBDA` | core | `AST_NODE_FUNC_EXPR` |
| `STARRED` | core | `AST_NODE_SPREAD` / `AST_NODE_REST_ELEMENT` |
| `KEYWORD_ARGUMENT` | core | `AST_NODE_NAMED_ARG`; `**splat` → SPREAD + `is_map_splat` *(var.)* |
| walrus `:=` | core | `AST_NODE_ASSIGN` in expression position |

**Statements (L2) and bindings (L3):**

| Python node | Disposition | Target + notes |
|---|---|---|
| `MODULE` / `BLOCK` / `EXPRESSION_STATEMENT` | core | `AST_SCRIPT` / `AST_NODE_BLOCK` (no own scope — no block scope in Python) / `AST_NODE_EXPR_STMT` |
| `ASSIGNMENT` (multi-target, tuple, annotated) | core+ | `AST_NODE_ASSIGN`; tuple/star → L3 `ARRAY_PATTERN`/`REST_ELEMENT` (one shared destructuring engine, U13); multi-target chains carry *(var.)* `next_target` — value evaluated once, never desugared |
| `AUGMENTED_ASSIGNMENT` | core | `AST_NODE_ASSIGN` with compound `op` (U19 single-evaluation) |
| first assignment to a name | builder-norm | synthesized declarator at bind time (design §L2 rule 1) |
| `IF`/`ELIF`/`ELSE` | core | `AST_NODE_IF_EXPR` statement-position; elif nests in `otherwise` |
| `WHILE`/`FOR` (+ `else`) | core+ | `AST_NODE_WHILE_STAM` / `AST_NODE_FOR_OF_STAM` (U22) + `else_body` *(var.)*; iterator protocol profile-dispatched |
| `BREAK`/`CONTINUE`/`RETURN`/`RAISE` | core | core kinds; `raise X from Y` cause as *(var.)* |
| `TRY`/`EXCEPT`/`FINALLY` | core+ | **`AST_NODE_TRY_STAM` upgraded to U23 multi-arm**: `{block; catch-arm list; else_body; finalizer}`; `AstCatchNode` gains `type_filter`; JS becomes the 1-arm case (PY3) |
| `ASSERT` / `DEL` | Py-range | `PY_AST_NODE_ASSERT` / `PY_AST_NODE_DEL` |
| `PASS` | builder-norm | dropped |
| `GLOBAL`/`NONLOCAL` | binding-only | consumed into `NameEntry.{is_global_decl, is_nonlocal}`; `PY_AST_NODE_SCOPE_DECL` marker kept for dump fidelity |
| `WITH` | Py-range | `PY_AST_NODE_WITH`; maps toward R1–R5 scoped-resource model later (PY8) |
| `IMPORT`/`IMPORT_FROM` | core | `AST_NODE_IMPORT` + specifiers; resolution stays profile policy |
| `MATCH`/`CASE` | core+ | `AST_NODE_MATCH_EXPR`/`ARM` + `MatchForm` + per-arm `guard` (U19) |
| `YIELD` (+ from) / `AWAIT` | core | `AST_NODE_YIELD` (`delegate`) / `AST_NODE_AWAIT` |

**Patterns (match/case → L3):** `PY_PAT_LITERAL/CAPTURE/WILDCARD` → core literal/ident/hole; `PY_PAT_SEQUENCE/MAPPING` (+rests) → shared `ARRAY_PATTERN`/`MAP_PATTERN`+`REST_*`; `PY_PAT_OR/CLASS/AS` → Py-range `PY_AST_NODE_PATTERN_OR/CLASS/AS`; `PY_PAT_VALUE` → `MEMBER_EXPR` in pattern position. Runtime support (`py_match_is_sequence`/`is_mapping`/`mapping_rest`) unchanged.

**Functions, classes, modules (L4–L6):** `FUNCTION_DEF` → `AST_NODE_FUNC` (`is_async`, `is_generator`, `is_proc = true` per U14); params → `AST_NODE_PARAM` + `default_value`, `*args` → `is_rest`, `**kwargs` → new *(var.)* `is_kw_rest`; decorators → tier-2 clause chain; `CLASS_DEF` → `AST_NODE_CLASS` with `superclass` generalized to `bases` list (design L5), metaclass as Py clause, methods → `AST_NODE_METHOD` (U25).

**Operators:** direct maps to `OPERATOR_ADD/SUB/MUL/DIV/MOD/POW/IDIV`, comparisons, `AND/OR/NOT/IN/IS`; bitwise+shifts reuse `OPERATOR_JS_BIT_*`/`JS_LSHIFT/RSHIFT` (neutral rename PY7); compound assigns reuse `OPERATOR_JS_*_ASSIGN`; new `OPERATOR_PY_MATMUL`/`_ASSIGN`; `not in`/`is not` builder-norm to `UNARY{NOT}` over the binary (safe — negates the result).

**Scope model:** `NameScope` per FUNCTION/MODULE only (no block scope); declare-on-first-assignment synthesis; `global` → `is_global_decl` (binds MODULE entry, module BSS slot); `nonlocal` → `is_nonlocal` (forces shared scope-env capture); `SCOPE_KIND_CLASS` (new — lookup skips CLASS scopes below a FUNCTION boundary, one shared-walk rule behind a profile predicate); `SCOPE_KIND_COMPREHENSION` (new — FUNCTION-like for capture analysis).

### 4.5 Core-catalog amendments Python drives

Each small, additive, justified by a second client per U3. Grouped by risk:

**Enum/range housekeeping (mechanical):**
1. Move `JS_AST_NODE_TS_EXTENSION_SENTINEL` 2000 → 1500 (`js_ast.hpp:134`, declared but unused) so Python owns 2000–2499 (PY1).
2. Add the Python block `PY_AST_NODE_* = 2000…` (~12 kinds: SET, COMPARE_CHAIN, ASSERT, DEL, WITH, SCOPE_DECL, PATTERN_OR/CLASS/AS, reserves).
3. ~~Fix the 541 collision~~ — **done** (`AST_NODE_EVENT_HANDLER = 542`, `ast-core.hpp:135`).
4. `Operator`: add `OPERATOR_PY_MATMUL`/`_ASSIGN`; optional neutral renames of `OPERATOR_JS_BIT_*` (PY7).

**Struct field additions (additive):**
5. `AstLiteralNode`: `int64_t int_value` union member + int/float `LiteralKind`.
6. `AstWhileNode`/`AstForOfNode`: `AstNode* else_body` *(var.)*.
7. `AstMatchNode`: `MatchForm`; `AstMatchArm`: `guard`, `fallthrough`.
8. SEQ `SeqKind` per U16.
9. `is_kw_rest`/map-splat flag for `**kwargs`.
10. `AstAssignNode`: `next_target` chain *(var.)*.
11. `AstClassNode`: `superclass` → `bases` list (JS builder produces 1-element list).
12. `NameEntry`: `is_global_decl`, `is_nonlocal`; `ScopeKind`: `SCOPE_KIND_CLASS`, `SCOPE_KIND_COMPREHENSION`.
13. `FnExt` union: add `py` member; per U20 upgrade members to typed forward-declared pointers.

**Structural upgrades (the two real ones — sequenced early in A2 because JS shares the structs):**
14. **Multi-arm `AST_NODE_TRY_STAM`** (U23): arm list + `else_body`; `AstCatchNode` gains `type_filter` (+ optional `guard`); JS try lowering iterates a 1-arm list. The plan's largest shared-code touch; also *the* U23 deliverable (PY3).
15. **Core `AST_NODE_INTERP_STR`** (U16): promote JS template kinds + per-element `format_spec` *(var.)*; JS tagged templates stay JS-range.
16. **Core `AST_NODE_FOR_EXPR` + clause nodes** `CLAUSE_FOR/WHERE/LET` (Lambda `GROUP/ORDER/JOIN` stay Lambda-range); Lambda takes the mechanical renumber (PY4).
17. **Core `AST_NODE_RANGE`** `{start; end; step; inclusive}`.
18. `lang_profile_for_name` (`ast-core.hpp:809`): add `py_profile`, resolve `"python"`.

### 4.6 Migration stages

Method: K17 extract-after-convergence — **`test/py` corpus green after every stage**; shared-header stages additionally gate on `make test-lambda-baseline` + the JS suites. No big-bang: `transpile_py_mir.cpp` shrinks family-by-family.

**Stage A0 — substrate completion** (no AST change; start now): (1) replace `PyMirVarEntry` + `py_var_scope` hashmaps + `var_scopes[64]` with shared `VarEntry` + `em_var_scope_new` (field-subset, mechanical); (2) move `module_consts` onto the emitter const-pool (`const_list`/`consts_bss`); (3) unify Python's module-var globals (`global_var_names/indices`) with the Lambda `global_vars` BSS model — prerequisite for A6 live bindings; (4) set `em.note_mir_call` for call telemetry (subsumed by Track F1b's callback enablement if F1 lands first — coordinate). Deletes ~250–400 lines; zero behavior change. Gate: `make build-jube` + py corpus; byte-diff `temp/py_mir_dump.txt` across the corpus.

**Stage A1 — kind-space entry** (mechanical): land §4.5 items 1–4; `typedef AstNodeType PyAstNodeType` with `PY_AST_NODE_X = AST_NODE_Y` aliases (the `js_ast.hpp` precedent) + real 2000-range residue values; convert `Py*Node` from `base`-member composition to `: AstNode` inheritance (mechanical `n->base.node_type` → `n->node_type`; `static_assert` layout pins); add a Python AST-dump golden (kind *names*, so renumbering is invisible). Gate: py corpus + dump goldens + lambda/js suites.

**Stage A2 — leaf-struct adoption, level by level**: L1a literal/ident/unary/binary/boolean (needs §4.5.5); L1b call/member/collections (+NAMED_ARG promotion, `SeqKind`); L1c f-strings (§4.5.15) + slices (§4.5.17); L2+L3 statements & the variable story (assignment family incl. tuple-unpack → L3 patterns, try multi-arm §4.5.14, match §4.5.7, declarator synthesis builder-side); L1d comprehensions (§4.5.16, after L2/L3); L4 functions (+`is_kw_rest`, decorator clauses); L5/L6 classes (+`bases`) and imports. Each sub-step one commit; `py_ast.hpp` shrinks to ~150 lines of residue. Gate per sub-step: py corpus; lambda/js suites when the shared struct changed.

**Stage A3 — binding on shared scopes**: replace `PyScope`/`py_scope.cpp` with shared `NameScope` (+CLASS/COMPREHENSION kinds); implement Python's binding rules at build time (declare-on-first-assignment, `global`/`nonlocal` consumption, CLASS-scope skip, comprehension isolation); `validate` hook takes over Python's static errors; builder-level unit tests for the scope rules. Gate: py corpus + new scope tests.

**Stage A4 — analysis on `FnAnalysis`**: split `PyFuncCollected`/`PyLambdaCollected` — capture facts → `FnAnalysis.captures`/`FnCapture` (nonlocal ⇒ `force_env_capture`), generator/async facts → `FnAnalysis` counts, Python-only facts → `PyFnExt`; adopt the shared capture pass when main Phase 3 lands (data model first, pass second — the JS two-step); implement `resolve_param_evidence` (int → `LMD_TYPE_INT64` native path, overflow promotion stays in `py_add`/friends — inference selects representation, never semantics). **This is PO8's landing site** (Track F interlock). Gate: py corpus; MIR-dump spot checks that native-int fast paths still fire.

**Stage A5 — `PyProfile` + shared-driver adoption** (syncs with main Phase 4): move lowering family-by-family into the shared driver + hooks in dependency order — statements skeleton, expressions, destructuring engine (replaces `pm_assign_comp_target` + per-site copies), calls (the ~1,100-line family incl. TCO onto the driver's shared TCO), functions/closures onto shared scope-env emission, classes/match/ext-nodes, imports last. **Generators/async explicitly deferred within A5** until the shared resumable-function transform (K17 layer 1) is extracted with its two green clients; Python then becomes its third client (PY6) — frame layout maps onto K17 layer-2 resume frames, whose *ownership* Track F2b already fixed. **Track F's frame emission moves into the driver here unchanged** (interlock §2.5), and PF1's finish-spine extraction + PF2's args convergence complete. Gate per family: py corpus; MIR-dump diff reviewed; no lambda/js regression. Escape hatch: git-revert per family, no runtime flag (PY9).

**Stage A6 — module system & cross-language binding** (syncs with main Phase 5): Python modules gain `ModuleDescriptor.ast`; Lambda/JS importing Python binds through `declare_module_import` (namespace-map shape walk retires); Py→Py imports bind at AST level; module top-level state onto unified BSS enabling live bindings; fix + un-skip `test_py_import` (filesystem-import Bus error); import *resolution* rules stay profile policy. Gate: py corpus incl. un-skipped import tests; cross-language import tests green.

### 4.7 What stays Python-owned (explicit non-targets)

Runtime library (~7,100 lines — the Item-level semantics contract per J5/J6); number-model policy (int64 + runtime bigint promotion); reference-semantics/aliasing projection rules (G7 — documented as explicit `PyProfile` policy); import resolution + stdlib registry; resumption drivers + calling conventions (K17 layers 3–4); grammar + the CST→AST builder file (retargeted, never shared).

### 4.8 Sequencing against the main unified-AST track

| Track A stage | Depends on (main track) | Can start |
|---|---|---|
| A0 substrate | nothing (Phase 0 landed) | **now** |
| A1 kind space | nothing | **now**, after A0 |
| A2 leaf structs | §4.5.14–17 coordinate with ast-core owner | now, level by level |
| A3 binding | nothing (shared scopes exist) | after A2's L2/L3 |
| A4 analysis | data model: now; shared *passes*: main Phase 3 | split accordingly |
| A5 driver adoption | main Phase 4 driver per family (Python trails Lambda/JS bring-up by one family) | as Phase 4 progresses |
| A5-generators | resumable transform extracted (two green clients) | after that lands |
| A6 modules | main Phase 5 | with/after Phase 5 |

If the main track stalls before Phase 4, Python still ends A4 with a unified AST/binding/analysis and its own lowering intact — a stable, shippable intermediate state. Track F is independent of the main track entirely.

### 4.9 Size accounting (targets)

| File | Today | After A0–A4 | After A5–A6 |
|---|---|---|---|
| `transpile_py_mir.cpp` | 7,644 | ~6,800 | **~0 — deleted**; replaced by `py_profile.cpp` ≈ 1,000–1,500 lines |
| `py_ast.hpp` | 545 | ~150 | same |
| `build_py_ast.cpp` | 2,429 | ~2,400 (retargeted in place) | same |
| `py_scope.cpp` | 252 | **deleted** (A3) | — |
| runtime library | ~7,100 | unchanged | unchanged |

---

## 5. Risk register (merged)

| Risk | Mitigation |
|---|---|
| Shared-header churn breaks Lambda/JS (A2 amendments; F1b spine extraction) | additive fields with defaults; `static_assert` pins; one amendment per commit; full three-suite gate each |
| Python scope subtleties regress silently (CLASS-skip, comprehension isolation) | test-first corpus additions before A3; builder unit tests |
| Chained-compare / multi-target desugar hazards | never desugared — Py-range node + *(var.)* chain (U19) |
| Native int fast path lost in A5 → perf cliff | micro-benches before A5; `lower_binary` inline contract; MIR-dump review per family |
| int64 vs bigint drift | inference selects representation only; promotion stays in `py_*` helpers |
| `push_d` float dangles across un-frameed returns | F3d hard-gated on F1a + F3b (v2 change) |
| Env layout migration (single-width → `size*2` SF18) misses an indexing site | F2a audits all env-slot arithmetic, not just the three alloc sites; GC-stress + ASan gate |
| Generator frame layout vs K17 resume frames | F2b fixes ownership now; layout rides the shared transform later (PY6) |
| Import ordering / circular imports in A6 | registry `loading` semantics preserved; un-skipped `test_py_import` |
| 7,644-line file migration stalls half-done | family-by-family commits, each green; measurable shrink per family (§4.9) |
| jube-only breakage invisible to `make test` | PY10: add `make test-py`/`test-jube` to the default extended path |

---

## 6. Gates (every stage, both tracks)

- `make test-jube` green (owns `test/py/` fixtures + goldens); `test/test_py_gtest.exe` all passing.
- **Lambda baselines byte-identical** (`make test-lambda-baseline`, currently 3,487/3,487) and **JS baselines unchanged** (`make test262-baseline`, currently 40,261/40,261, zero unstable; JS gtest green) — the two-runtime protection contract. (`make node-baseline` is the Node-compat tracking number, not a gate — too noisy.)
- Shared-header edits (ast-core, mir_emitter_shared, sys_func_registry): the full three-suite sweep above.
- F1+: `py` entry stack-overflow fail-fast behaves like the Lambda/JS entries; Python joins `make test-gc-rooting` as its fifth lane.
- F2/F3: GC-stress + ASan over `test/py/`; churn/soak profiles as listed per stage.
- F0a onward: the int-overflow promotion fixture stays green permanently.
- Before F0a: capture the §7 exit-requirement baselines (representative-frame MIR/local counts from `temp/py_mir_dump.txt`; production LOC via `./utils/loc_report.py lambda lib`, hand-written total) — both numbers must be recorded before any change lands.
- **Gate zero (before anything behavior-affecting): restore the 14 orphaned goldens** — generate from today's implementation (current behavior *is* the baseline being protected); fix or consciously retire failures. A 25-pair gate is too thin for either track.
- Corpus additions test-first, before the stage that touches them: walrus in comprehensions; chained comparison with side-effecting middle operand; `while`/`for` `else` ± `break`; class-var shadowing, two-level `nonlocal`, comprehension non-leakage, module-level `global`, `del` of a local; `a = b = f()` (f once); aug-assign on subscript with side-effecting index; or/class/guard patterns, star patterns per position; `**` merge order, keyword-only params.
- **≥80%-core acceptance metric** (Track A): `--ast-stats` builder counter, %-of-occurrences on core kinds at A2 exit; target ≥85%.
- **Performance:** 2–3 micro-benches (fib/TCO, float loop, string-build) before A5; release build (`make release-jube`) for timing per rule 10; Track F million-iteration wide-scalar probes per the Stack API gate set.

---

## 7. Success criteria

**Exit requirements (measured, both mandatory — the Stack API precedent):**

1. **MIR instructions per generated Python frame must reduce** by the end of the implementation, net of the added frame prologue/epilogue. Method: pick 2–3 representative release-build frames (e.g. a closure-bearing function from `test_py_closures.py`, a hot arithmetic loop body, a generator body), record baseline MIR-instruction and local counts from `temp/py_mir_dump.txt` before F1, and report the same table shape as `Lambda_Design_Stack_API.md`'s final frame profile (Lambda `_frame_review_0` −22.8%, JS `_js_frameReview_149_body` −17.6%) at F4c/A5 exit. Frames add prologue/epilogue cost, so the reduction must come from what the shared machinery retires (redundant boxing, per-call ALLOCA sequences, duplicated call bookkeeping) — an intermediate stage may regress, the exit may not.
2. **Total production C/C++ LOC must reduce** — measured with `utils/loc_report.py` (comments, blanks, and `log_*`/printf lines already excluded; supports sub-dir filtering): `./utils/loc_report.py lambda lib`, using the **hand-written** total (vendored/generated reported separately by the tool). This controls *production code only* — `./test` is out of scope: the tool can report its LOC, but test growth neither counts against the requirement nor offsets it. Baseline recorded before F0a; checked at each track's exit. Track A's §4.9 size accounting (~349 KB lowering stack → ~40–60 KB profile) makes the A-side trivially satisfiable; the requirement bites on Track F, which must land frames/rooting by *reusing* shared machinery, not by adding a third copy — consistent with the Stack API's own exit (source scope 7 lines smaller than its baseline).

**Track F:** Python-generated code runs with precise side-stack rooting and frame-scoped numbers through the same shared `MirEmitter` frame subsystem as Lambda and JS (composed per PF1, with call signatures and callbacks live); closure envs and generator/coroutine frames are GC-owned and reclaimed on the `GC_TYPE_JS_ENV` layout (the pool-pinning invariant deleted, not just documented); call args no longer use `MIR_ALLOCA`; the `py` entry has a real `LambdaRecoveryCheckpoint` boundary; int56 overflow promotes to BigInt; helper floats are inline via `push_d` under the adoption contract — with Lambda and JS behavior byte-identical throughout, and Python no longer the blocker for SF9 conservative-scan retirement.

**Track A:** Python parses to the common AST (≥85% core-kind occurrences), binds on shared `NameScope`, analyzes into `FnAnalysis`+`PyFnExt`, and lowers through the shared driver + `PyProfile`; the ~349 KB parallel-lowering stack collapses to a ~40–60 KB profile; cross-language imports bind at AST level with live bindings — the design's §7 economic claim, made checkable per stage.

---

## 12. Decision points

Track F: **PF1–PF3** (§3, table at end of Track F).

Track A (carried from the absorbed doc, pending user confirmation):

| # | Decision | Recommendation |
|---|---|---|
| **PY1** | Py kind range 2000–2499 with TS sentinel moved to 1500 vs Python at 2500 | move TS (one unused line) — keep the design's table true |
| **PY2** | Chained comparison as Py-range node | as stated — desugar is a U19 hazard |
| **PY3** | Multi-arm `AST_TRY` (U23) done in this port, JS = 1-arm case | yes — Python is the second client that makes U23 concrete |
| **PY4** | `INTERP_STR` + `FOR_EXPR`/clauses + `RANGE` + `NAMED_ARG` core promotions in A2 | yes — each has ≥2 producers after this port |
| **PY5** | Generator exprs builder-normalized to `AST_FUNC{is_generator}` closures | yes — matches CPython, reuses generator path |
| **PY6** | Generators/async wait for the shared resumable transform (Python = third client) | yes — avoids migrating ~1,800 lines twice |
| **PY7** | Neutral renames `OPERATOR_JS_BIT_*` → `OPERATOR_BIT_*` (aliases kept) | yes, mechanical, in A1 |
| **PY8** | `with` stays Py-range until R1–R5 defines the core node | yes — promote later |
| **PY9** | A5 family cutover without runtime fallback flag (git-revert; jube-only blast radius) | yes |
| **PY10** | `make test-py`/`test-jube` in the default extended test path | yes |

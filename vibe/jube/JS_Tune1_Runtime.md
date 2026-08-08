# JS Tune1 Runtime — Implementation Plan: In-Band Exception Refactoring (R2a/R2b)

**Date**: 2026-08-07  **Status**: IMPLEMENTED — E8 validation complete
**Tree anchor**: master `b9b30f4ac`
**Design authority**: `vibe/jube/JS_Runtime_Redesign.md` JR3/JR3.1/JR3.2 (all
rulings), §6 phases R2a/R2b. History: G0–G3 per JR3;
`vibe/Lambda_Impl_Online_Exception (done).md` (G2, superseded in place).
**Baseline evidence**: `vibe/jube/JS_Profiling_Helpers.md`.

**Decision.** The LJS exception path now uses Lambda's in-band merged-lane
ABI: every fallible JS helper returns a success Item or an ERROR-tagged
Item, and MIR propagation branches on that returned value. JS `try`/`catch`
consumes the same carrier without a pending flag or a second polling channel.
The `LambdaError` carrier is Map-compatible at rest, preserves JS payload
identity, and materializes its stack lazily. This implements JR3/R2a/R2b and
the normative contract in S7.4.4 and D8.4.3.

---

## 0. Scope decisions (the two open questions, answered)

**What goes before / with the exception work:**

- **Before — E0 census + E1 promise-rooting quick win.** E0 is read-only
  tooling (the helper error-contract census that generates the sweep
  worklist). E1 is Tune1-Helpers **P1a** (lazy promise root registration):
  ~one function, independently testable, and it removes the top measured
  cost (7.1% of CPU) so every later profile in this plan measures the
  exception work instead of known noise. Measurement hygiene, not scope
  creep.
- **Deliberately NOT pulled forward:** R1 (JsName) — orthogonal to
  exceptions; no dependency, only shared churn. R3 (builtins-as-values) —
  the redesign notes R3-first would shrink this sweep, but R3 is a larger
  phase than R2 itself; gating the first cut on it inverts priorities. Cost
  accepted: the sweep converts pending-checks inside dispatch-switch bodies
  that R3 later deletes — bounded (a subset of the 400 js_runtime.cpp sites),
  mechanical, and the conversion pattern is identical either way.
- **R2b rides in this plan** (E7): after the Q1b ruling it depends only on
  the shared class-Error shape and a `.stack` accessor — existing Map/shape
  machinery — so it follows E6 directly with no JR4 slice.

**Tune1-Helpers disposition** (per phase):

| Tune1-Helpers | Disposition |
|---|---|
| P1a lazy promise root registration | **Merged here as E1** (quick win + measurement hygiene); full JR7 (VMap promise) stays a later phase |
| P5 exception-effect catalog tightening | **Merged here** — woven into E0/E4/E6; under in-band it is branch-elision fuel (rung (a) of the check ladder), and the census walks every row anyway (today: 15 `PRESERVES` of 505 `js_*` rows) |
| P2 key plumbing / dense arrays, P3 prototype cache, P4 class instantiation | **Not merged** — superseded by redesign phases R1/R4/R5; implementing them twice would violate the retirement discipline |

`JS_Tune1_Helpers.md` gets a status note recording this disposition.

## 1. Implemented migration

The work landed as a flag-day cleanup after the helper census established the
fallible surface. The former pending flag, `js_check_exception`/clear protocol,
transition environment switch, and exception coherence tripwires are deleted.
`jm_emit_error_lane_test` uses the call-result register for unknown effects;
catalog-proven `PRESERVES` calls still elide the branch through
`error_lane_track`.

The routed error register is stable for the lifetime of a try context. This is
important for nested calls: fallback error values are written into the
context's original register rather than replacing the register field after
catch lowering has captured it.

## 2. Work plan

### E0 — Census, baselines, primitives — implemented

- **Baselines frozen**: 327-suite, js262 gate, node baseline, GC-stress;
  matched profile pair on release_profile (§6 protocol of the profiling doc);
  wall time of the 261-script batch (34.4s baseline).
- **Helper error-contract census** (script in `utils/`): join the emitted
  helper list with `sys_func_registry.c` rows → for each helper: return kind
  (Item / void / raw scalar) × `exception_effect`
  (`MAY_SET`/`PRESERVES`/`CLEARS`/`SETS`, enum `sys_func_registry.h:92`) ×
  whether its body can reach `js_throw_value`. Outputs:
  1. the E3 sweep worklist (batched by file),
  2. the **void-fallible list** (each becomes Item-status or is proven
     infallible),
  3. the **raw-scalar audit** (4 known raw-double helpers: `js_get_number`,
     `js_math_ceil_d`, `js_math_pow_d`, `js_math_round` — verify infallible,
     mark `PRESERVES`; the "only infallible helpers may return raw scalars"
     rule becomes a census lint that stays forever),
  4. `PRESERVES` candidates for the catalog audit (P5): rows that provably
     cannot throw, upgraded row by row — never by default (D6.1.3 polarity).
- **Primitives**: confirm/add the `item_is_error(Item)` tag test used by all
  converted checks; add the D2 GC note's obligation (ERROR-tagged Items
  traced as heap references) with a GC-stress case.

Result: the census drove the helper conversion and the raw-scalar audit.

### E1 — Quick win: lazy promise root registration (Tune1-P1a) — implemented

`js_promise_register_roots_once` (`js_runtime.cpp:31904`) → register a slot's
7 ranges at first allocation via epoch-scoped high-water mark
(`js_promise_count` is monotonic per epoch; register **before** the slot can
hold heap pointers, i.e. before `js_alloc_promise` returns).

Result: promise root slabs are registered by epoch-scoped high-water mark,
before a newly allocated slot can contain a heap Item. The registration count
resets with the heap epoch.

### E2 — The throw seam (runtime state core) — implemented

- `Item js_throw_value(Item v)` is the single carrier constructor. All convenience throwers
  (`js_throw_type_error`, `js_throw_named_error`, `js_check_tdz`,
  `js_throw_const_assign`, …) return the resulting Item directly.
- `JsRuntimeState` has no exception carrier slots or message buffer; ordinary
  JS throws carry only their returned ERROR Item. `js_error_lane_payload` is
  the explicit catch/rejection/host-boundary unwrap.

### E3 — Helper sweep (batched by file, census-driven) — implemented

Convert each helper body: legacy ambient-state checks → `item_is_error(r)` on
the actual call result; error paths `return` the error Item instead of a
null-with-flag. Site counts (measured): `js_runtime.cpp`
400, `js_runtime_value.cpp` 41, `js_globals.cpp` 12, `js_runtime_state.cpp`
11, `js_child_process.cpp` 3 — plus the **210 `js_clear_exception` refs in 17
files** (Node/DOM compat layers: fs 16, dom_events 5, dns 3, assert 23, …)
where check-and-clear becomes consume-the-value (error-first callbacks,
rejection values).

Batch order (each batch = one commit, both modes green):
1. coercion/value core (`js_runtime_value.cpp`, `js_coerce.cpp`),
2. `js_runtime.cpp` in subsystem slices (property → array → string → call →
   class → builtins-dispatch last, since R3 will delete much of it),
3. `js_globals.cpp` + `js_props/attrs`,
4. Node/DOM compat files (the 17 clear-consumer files),
5. void-fallible conversions from the census list.

Result: fallible void/status helpers were converted to Item results or proven
infallible. Node, DOM, crypto, stream, buffer, filesystem, network, and
descriptor paths now propagate the actual error Item.

### E4 — Lowering flip — implemented

- `jm_emit_error_lane_test` (`js_mir_completion.cpp`): the `UNKNOWN` arm
  in mode 1 tests the **tag of the just-returned call result** instead of
  calling `js_check_exception` — the call-result register is plumbed from
  call emission to the propagate check
  (`jm_emit_error_lane_propagate_check`, `js_mir_statement_lowering.cpp`).
  Helpers with raw-scalar returns emit no check (census-verified infallible).
  `CLEAN`/`SET`/`UNREACHABLE` folding and the whole `error_lane_track` proof
  are **unchanged** (they elide the branch entirely — the ladder's rung (a));
  generator/state-machine save/restore stays as is
  (`js_mir_function_class_lowering.cpp:965/:1968`).
- **Propagation across JIT frames**: the per-function `func_error_lane_label`
  path returns **the error Item itself**. A JS function
  returning an error Item *is* the signal; `js_call_function` passes it
  through untouched. This is the central semantic change — one merged lane
  through the whole call graph, exactly Lambda's `T^E` ABI.
- **try/catch/finally rewrite** (statement lowering `:5483–:5824` region):
  catch entry receives the routed error value in a register/slot and binds
  the catch parameter by **retag** (no `js_clear_exception`); finally saves
  the in-flight value in a local slot, runs, re-raises by returning it
  (finalizer's own throw takes precedence per spec — same routing as today);
  abrupt-completion cleanup (`jm_emit_abrupt_jump_cleanup`) and the
  arg-frame reclamation on throw-during-argument-evaluation
  (`js_args_save/restore`) keep their existing structure with the value
  riding alongside.
- Async/generators: completion records carry the error **value** (they
  already carry Items); resume paths re-inject it as the merged-lane return.

Result: MIR dumps contain tag branches and no `js_check_exception` imports.
Nested function, arrow, accessor, generator/async, and finally paths route the
original Item identity.

### E5 — Boundaries and hosts — implemented

- The four out-of-tree translators stop translating:
  `lambda/jube/jube_registry.cpp`, `lambda/runtime/sys_func_registry.c`,
  `radiant/script_runner.cpp`, `radiant/event.cpp` — value passthrough per
  JR3.1's interop dividend (a JS throw arrives in Lambda as an ordinary
  `T^E` error; `^`/`^err` consume it).
- Entrypoints: `js_main`/`js-test-batch` return the error Item; runner
  reporting reads it directly; batch protocol unchanged (exit codes already
  derived from result state).
- Fault regime untouched: stack-overflow/OOM keep the
  `LambdaRecoveryFrame` path, converting to the pre-created error at the
  recovery boundary as today.

Result: Jube, Radiant, module, and Node host boundaries carry Item results
without translating the former pending exception state.

### E6 — Deletion day — implemented

- Remove: poll emission (mode 0), `LAMBDA_JS_INBAND_EXC`, the pending flag,
  exception slots/message buffer, the flag half of the tripwires,
  `js_check_exception` from the helper ABI (0 emitted sites), and dead
  `js_clear_exception` forms. No ambient error carrier remains.
- Catalog audit lands (P5): census-proven rows flip to `PRESERVES`
  (15 → target from census; each mechanically verified), shrinking emitted
  branches.
- **Rule-17 landing**: §8's D-ruling into `doc/Lambda_Formal_Design.md`
  (semver bump); `doc/dev/js/JS_04_MIR_Lowering.md` §9 rewritten for the
  in-band model (G3 note added to the poll-reduction paragraph);
  `JS_Runtime_Redesign.md` R2a marked implemented.

Result: the error-channel census is **1** and emitted exception-poll sites
are **0**. The legacy symbols and transition switch are absent from the JS,
Jube, host, and Radiant paths.

### E7 — R2b: payload unification — implemented

- `LambdaError` gains the **Map-compatible prologue** (TypeId=`LMD_TYPE_MAP`
  + `TypeMap*`, the JsPromise header pattern made principled); constructors
  in `lambda-error.cpp` initialize it; **static C14 fault records carry a
  NULL shape patched at first surfacing** (the recovery boundary already runs
  conversion code).
- **Shared class-Error shape** built at realm boot; property rules (dual-truth
  avoidance): JS-born errors materialize spec-visible own **data**
  properties at construction (`message`, and `cause` when provided) —
  descriptor-exact; Lambda-born errors surface `message`/`cause` through
  shape **accessors** reading the struct (de-facto compatible); `name` +
  `instanceof` via the **code ↔ class prototype table** (bidirectional: JS
  constructors assign `code` and fill `location` from the throw site);
  user expandos via the lazily-attached overflow (rides `details`);
  `thrown_value` carrier for non-object throws (reusable, identity
  unobservable).
- **Lazy stack** (Q1a ruling): split `err_capture_stack_trace`
  (`lambda-error.cpp:582`) into walk-only raw-PC capture (construction) and
  the existing two-tier resolution + formatting (first `.stack` read via the
  shape accessor); `stack_trace` becomes tri-state (raw PCs → `StackFrame`
  list → string); Lambda-eval capture sites (`lambda-eval.cpp:152` et al.)
  switch to raw capture — the Lambda side inherits the lazy win.
  Pre-created singletons skip capture.
- Converge `js_new_error*` constructors on the unified struct; delete
  today's Error-map construction (`js_promise_to_item`-style wrappers for
  errors, `js_new_error_with_name_stack`'s eager symbolization).

Result: Error values use the Map-compatible `LambdaError` prologue, retain
the thrown-value carrier, expose spec-compatible own descriptors, and lazily
materialize stack text. `instanceof`, accessors, primitive throws, and
pointer identity are covered by `test/node/js_tune1_error_lane.js`.

### E8 — Final validation & measurement — complete

The focused fixture, debug/release builds, forced-GC probes, MIR ratchet, and
static census are green. The broader Node official matrix was also executed;
its remaining failures are outside Tune1's error-lane scope and are recorded
below.

## 3. Validation summary

- **3.1 Behavior**: the focused Tune1 fixture is golden and covers Error
  identity, primitive throws, nested arrow throws, accessor throws, lazy
  stack, descriptors, `instanceof`, and finally precedence.
- **3.2 Census**: error channels **1**; emitted exception-poll sites **0**;
  `js_check_exception`, `js_clear_exception`, and the pending-state symbols
  have no remaining JS/Jube/host references.
- **3.3 ABI**: raw-scalar helpers remain outside the fallible Item lane;
  fallible status helpers return Item results and callers propagate them.
- **3.4 GC**: promise root registration is epoch-scoped and lazy; Error
  carriers and raw stack storage are rooted/owned at their representation
  boundaries.
- **3.5 Release**: the Tune1 fixture passes in the optimized build, including
  forced-GC and poison-freed mode.

## 4. Risks & tripwires

- **Widest mechanical change in the tree**: mitigated by the helper census,
  focused fixtures, and direct Item-result propagation.
- **Nested completion routing**: the try-context incoming register is stable;
  fallback values are written into it, preventing a stale callee register from
  replacing a nested thrown value.
- **Generator/async resume**: completion records must carry the error value
  through suspension; covered by async/generator fixtures + the existing
  `exc_track` save/restore discipline (unchanged).
- **`CLEARS`/`SETS` effect rows**: the census-driven conversion is complete;
  the static census remains the guard against reintroducing a second channel.
- **Descriptor-exactness in E7** (data vs accessor for `message`): decided
  above (JS-born = data, Lambda-born = accessor); js262 error fixtures gate
  it — if a conformance case objects, Lambda-born errors materialize data
  props at surfacing instead (one-line fallback, recorded here in advance).
- **Raw-PC stack lifetime** (E7): valid by `script->debug_info` ownership;
  a debug assert on materialization verifies the table generation matches.

## 5. Landing checklist (per rule 17)

- E6: D8.4.3 added to `Lambda_Formal_Design.md` and the spec version bumped;
  `JS_04_MIR_Lowering.md` §9 now documents the in-band path.
- E7: JR3.2 is marked implemented; `LambdaError` prologue and lazy stack are
  documented here and in the runtime redesign record.
- E8: focused, GC, MIR-ratchet, release, and broad Node-matrix validation are
  complete; the broad matrix's unrelated compatibility failures are retained
  as evidence rather than folded into Tune1.

## 6. Open items

No Tune1 design or validation items remain. The broader Node matrix is not a
Tune1 pass/fail gate: it completed with 2,682 passes, 368 skips, and 379
compatibility failures in unrelated subsystems.

## 7. E8 evidence appendix

Commands already passing:

```text
make build
./lambda.exe js test/node/js_tune1_error_lane.js --no-log
./lambda.exe js -e '<nested arrow, accessor, primitive, finally probes>'
make test-jube-node-error-lane
make build-release
./lambda.exe js test/node/js_tune1_error_lane.js --no-log  # optimized binary
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./lambda.exe js \
  test/node/js_tune1_error_lane.js --no-log
./test/test_mir_ratchet_gtest.exe
make node-regression-gate  # requires network-capable execution for sockets
```

Static checks already passing:

```text
rg js_exception_result|js_exception_pending|js_check_exception|js_clear_exception
    lambda/js lambda/jube radiant test/node
```

The focused fixture's expected output is checked in beside the script.

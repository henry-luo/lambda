# Lambda Impl Plan — Online Exception-Poll Tracking (JS MIR)

**Date**: 2026-07-23  **Revised**: 2026-07-24  **Status**: PLANNED  **Tree anchor**: master `f8e454d75` (line refs below are as of this commit; anchor by function name when they drift)

**Decision**: Replace the blind-emit-then-optimize exception-poll scheme with an **online abstract state** consulted at emission time. Status reads are folded, route polls are skipped, and definitely-set routes are strengthened to unconditional jumps directly at emission. The adjacent-poll peephole and `jm_optimize_exception_polls` dataflow pass are deleted only after differential validation against the legacy pipeline. Runtime debug tripwires guard both false-clean and false-set proofs.

---

## 0. Context

Current mechanism (all from the 2026-07-20 tuning commit `8840e937c` unless noted):

1. **Blind emission**: per-statement polls in try bodies (`js_mir_statement_lowering.cpp:5772/5778`), async bodies (`js_mir_function_class_lowering.cpp:3680`), class-body statement loops (`js_mir_function_class_lowering.cpp:2156`), plus ~30 special-site polls (inventory §2).
2. **Adjacent peephole**: `last_exception_poll_branch/_target` in `jm_emit_pending_exception_check` (`js_mir_completion.cpp:148`) — dedupes only literally-adjacent same-target polls, only for helper-emitted ones.
3. **Offline pass**: `jm_optimize_exception_polls` (`js_mir_completion.cpp:218`, ~310 lines) — per-function CFG + fixed-point cleanliness dataflow + poll deletion, run at function finalization (`js_mir_hashmap_scope_utils.cpp:453`). Allocates ~10 instruction-count-sized arrays per function; already needed a poll-free early-exit because CFG construction "dominates compile time in large module batches".

Costs of the current design: emit-then-delete churn, per-function CFG/fixed-point compile time, and two overlapping mechanisms. The pass also cannot exploit *definitely-set* knowledge at conditional throw-helper sites. Explicit `throw` is already lowered as `js_throw_value` plus an unconditional transfer; its remaining opportunity is the unreachable per-statement poll emitted afterward.

Key enabling facts (verified 2026-07-23):

- **Resolved import/direct calls flow through one hook**: named calls, void calls, and metadata-backed direct-pointer calls all end in `em_after_resolved_call` (`mir_emitter_shared.hpp:2760/2791/2939`) with `effects.exception`. Raw/unclassified `MIR_CALL`/`MIR_JCALL` instead flow through `em_emit_unclassified_call`; the tracker must cover both paths.
- **All labels flow through one function**: `jm_emit_label` (`js_mir_hashmap_scope_utils.cpp:490`).
- **Effect metadata already exists per helper**: `JitImport` rows in `sys_func_registry.c`; `exception_effect` field defaults to `JIT_EXCEPTION_MAY_SET = 0` (conservative-by-default), some rows explicitly `PRESERVES`. Enum in `sys_func_registry.h:86`.
- The pass assumes **function entry is dirty** (`entry_clean[0] = false`, `js_mir_completion.cpp:413`); the tracker must mirror this.

---

## 1. Design (OE1–OE10)

### OE1 — State

```c
// js_mir_context.hpp
typedef enum JsExcTrack {
    JS_EXC_UNKNOWN = 0,   // flag may be clear or set — conservative default
    JS_EXC_CLEAN,         // flag provably false
    JS_EXC_SET,           // flag provably true
    JS_EXC_UNREACHABLE,   // code after an unconditional transfer; next label revives
} JsExcTrack;
// field: JsExcTrack exc_track;   on JsMirTranspiler
```

Function entry state = `UNKNOWN` (mirrors the pass's dirty entry). `exc_track` is part of the per-function context that is **saved/restored around every nested function/state-machine compilation**, alongside each existing save/reset/restore of `func_except_label` (`js_mir_function_class_lowering.cpp:733/752`, `1749/1768/2218`, and the generator/main resets).

### OE2 — Transition rules

| Event | New state |
|---|---|
| function entry / nested-fn entry | UNKNOWN |
| call with `JIT_EXCEPTION_PRESERVES` | unchanged |
| call with `JIT_EXCEPTION_CLEARS` | CLEAN |
| call with `JIT_EXCEPTION_MAY_SET` | UNKNOWN, regardless of prior state |
| call with `JIT_EXCEPTION_SETS` (new, OE4) | SET |
| exception status read only | state unchanged |
| route/guard false (fall-through) edge | CLEAN |
| route/guard true edge | SET |
| `jm_emit_label` (plain) | UNKNOWN |
| `jm_emit_label_with_state(l, s)` | `s` (construct-supplied merge) |
| after `jm_emit` emits or rewrites an unconditional `MIR_JMP` / `MIR_RET` | UNREACHABLE |

Merge for construct-supplied states: `merge(UNREACHABLE, x) = x`; `merge(CLEAN, CLEAN) = CLEAN`; `merge(SET, SET) = SET`; anything else = UNKNOWN.

`JIT_EXCEPTION_MAY_SET` is an unknown-effect default, not a "sets but never clears" contract. Preserving SET across it would be unsound. If later profiles justify more precision, add a separately audited `JIT_EXCEPTION_MAY_SET_NO_CLEAR`; do not overload `MAY_SET`.

### OE3 — Chokepoints and discipline

1. **All call effects**: new `MirEmitter` callback `note_call_exception(void* owner, JitExceptionEffect effect)`. Invoke it from both `em_after_resolved_call` and `em_emit_unclassified_call`; the latter reports `MAY_SET`. JS sets it in `jm_create_mir_transpiler`; Lambda/Py leave it NULL. The callback must run for void calls before any result-dependent early return.
2. **Status observation**: `jm_emit_exception_test()` is the only function allowed to emit `js_check_exception`. It returns a boolean MIR register:
   - CLEAN: debug-assert clear, materialize `0`;
   - SET: debug-assert set, materialize `1`;
   - UNKNOWN: emit `js_check_exception`, retain UNKNOWN;
   - UNREACHABLE: materialize a placeholder `0`, retain UNREACHABLE.
3. **Control refinement**: `jm_emit_exception_route()` and `jm_emit_exception_guard()` build on the status-test helper and own the edge refinement to SET/CLEAN. Saved-exception and iterator-close protocols use the status-test helper directly because they consume the boolean as data or execute cleanup on the exceptional edge.
4. **Control flow**: `jm_emit` marks every unconditional jump/return UNREACHABLE; `jm_emit_label` / `jm_emit_label_with_state` revive and merge states.

Discipline rule: no lowering code writes `mt->exc_track` directly. Only `jm_exc_*`, observation/control helpers, `jm_emit`, and label helpers touch it. Grep the source for both `exc_track` and raw `"js_check_exception"` calls; after E0, the latter must occur only in `jm_emit_exception_test` and the debug runtime assertions.

### OE4 — New effect class `JIT_EXCEPTION_SETS`

Append to `JitExceptionEffect` (`sys_func_registry.h`). Audit rule: SETS means the helper **unconditionally** leaves the flag set on **every** return path. Initial audited list (verify each body at impl time):

| Helper | New class |
|---|---|
| `js_throw_value` | SETS only after proving/restructuring its post-set work cannot clear the flag |
| `js_throw_named_error` | SETS if every construction path reaches a proven SETS primitive |
| `js_throw_const_assign` | SETS if every construction path reaches a proven SETS primitive |
| `js_clear_exception` | CLEARS |
| `js_check_exception` | PRESERVES |
| `js_require_object_coercible`, other conditional throwers | stay MAY_SET |

`js_throw_value` currently sets `js_exception_pending` before formatting the message and reading `name`/`message`. Its SETS audit must include those subsequent calls and reentry behavior. If that proof is awkward, split out or add a small no-fail setter whose final action establishes the flag. Do not classify from the helper name alone.

Note: `js_throw_type_error` / `_range_error` / `_code` variants are C-side helpers mostly called from C, not JIT imports — classify only rows that exist in `jit_runtime_imports`. The value flows through the Jube catalog as `uint32_t` (`jube_registry.cpp:1436`) — additive, no ABI break; the legacy pass treats SETS as unknown/dirty, so phases can land in either order safely.

### OE5 — Poll emission semantics

Two control helpers in `js_mir_completion.cpp`, built on `jm_emit_exception_test`:

- **`jm_emit_exception_route(mt, JsMirCompletionKind kind)`** — today's `jm_emit_pending_exception_check`, renamed to make call sites self-describing. Routes to the completion target (catch / finally / `func_except_label`).
- **`jm_emit_exception_guard(mt, MIR_label_t target)`** — for local skip-guards where exception-true branches directly to a construct-private label. It does **not** unwind the argument stack.

Both consult `exc_track`:

| State | Route behavior | Guard behavior |
|---|---|---|
| CLEAN | debug assert clear; emit no branch | debug assert clear; emit no branch |
| UNREACHABLE | emit nothing | emit nothing |
| UNKNOWN | status test; route using today's branch/unwind shape; fall-through → CLEAN | status test; `BT target`; fall-through → CLEAN |
| SET | debug assert set; `js_args_restore` if in arg scope; unconditional `MIR_JMP target`; state → UNREACHABLE | debug assert set; unconditional `MIR_JMP target`; state → UNREACHABLE |

Only route exits inherit the argument-stack unwind block currently inside `jm_emit_pending_exception_check` (`js_mir_completion.cpp:157–177`). Local guards preserve their existing stack behavior. Helpers such as `js_args_restore` used between a SET proof and its jump must be explicitly audited `PRESERVES`; an unknown-effect call invalidates SET.

### OE6 — Label rule

`jm_emit_label`: `exc_track = UNKNOWN` (any label may be a join from an unknown predecessor — generator resume states, break/continue targets, exception edges). New `jm_emit_label_with_state(mt, label, state)` is allowed only for constructs that own **all** predecessors and can supply their true merge (OE7). UNREACHABLE never survives a label.

In debug builds, structured labels register their expected predecessor count/states and assert that no unregistered edge targets the label. If predecessor registration is too invasive for v1, use `jm_emit_label_with_state` only for narrowly audited local labels and retain plain labels elsewhere.

### OE7 — Structured merges (v1 scope)

Merge API: `JsExcTrack jm_exc_state(mt)`, `void jm_exc_set_state(mt, s)` (helper-internal), `JsExcTrack jm_exc_merge(a, b)`.

Every structured split follows the same emission discipline:

1. Capture the state at the branch point after lowering/checking the condition.
2. Begin each independently emitted successor with that captured edge state, not with the state left by the previously emitted arm.
3. Capture each arm's exit state after lowering it.
4. Emit the owned join with the merge of all predecessor states, including the implicit no-else/short-circuit edge and UNREACHABLE arms.

| Construct | Treatment |
|---|---|
| if/else join | restore branch-point state at each arm entry; merge then, else, or implicit false-path state |
| `&&` / `||` / `?:` / `??` joins | restore the captured short-circuit/RHS edge states; merge every result-producing predecessor |
| catch label | `jm_emit_label_with_state(catch_label, JS_EXC_SET)` — entered only via exception edges; the `js_clear_exception` call at catch entry then flips CLEAN automatically via chokepoint 1 |
| await internal join (`after_await_label`) | merge(fast-path, resume-path) — both CLEAN after their polls → CLEAN (§4a) |
| loop head labels | plain label → UNKNOWN (conservative; body unknown at head-emission time) |
| loop exit / break targets | v1: plain label → UNKNOWN. Optional later: merge of registered break-site states |
| finally label | plain label → UNKNOWN (normal + exceptional predecessors; saved-exception protocol observes and clears explicitly) |
| generator/async resume labels (`gen_state_labels`) | plain label → UNKNOWN (resumption may inject a rejection) |

### OE8 — Saved-exception and cleanup protocols

Finally, `using`, and iterator-close sequences that **test, save, clear, run cleanup, and re-throw** are not ordinary route/guard sites:

- Reads whose result is saved (`saved_exc_flag`) use `jm_emit_exception_test`; CLEAN/SET may fold to constants.
- "New exception takes precedence" tests use `jm_emit_exception_test` plus their existing manual branch structure.
- `jm_emit_iterator_close_on_exception*` remains a protocol helper: on the exceptional edge it saves/clears the original exception, runs close, suppresses the close exception where required, restores the original, then routes. Its entry test is centralized, but it is not replaced by a direct guard.
- Each protocol explicitly supplies the state at its private labels. After `js_clear_exception`, the state is CLEAN; after the restoring `js_throw_value`, it is SET only when that helper has a proven SETS contract, otherwise UNKNOWN.

There is no `force_emit` boolean. The status-test API preserves the data dependency while still folding proven CLEAN/SET states safely.

### OE9 — Deletions (final phase)

- peephole fields + logic: `last_exception_poll_branch/_target` (`js_mir_context.hpp:445`, `js_mir_completion.cpp:148–154`, reset in `js_mir_hashmap_scope_utils.cpp:358`)
- `jm_optimize_exception_polls` + `jm_exception_poll_result` + `jm_branch_uses_poll_result` + `jm_merge_exception_predecessor` + the call at `js_mir_hashmap_scope_utils.cpp:453`
- `MirGcCallSite.is_exception_poll` field, the `strcmp(call_name, "js_check_exception")` classification (`mir_emitter_shared.hpp:2611–2615`), and the extra `em_root_note_call_site` parameter (touches lambda + py signatures, mechanical)
- **Keep** the `em_root_*` CFG utilities — root write-back liveness (`jm_finalize_write_back_roots`) still uses them.

### OE10 — Debug tripwires and differential mode

In debug builds:

- every CLEAN fold/skip emits `js_debug_assert_exception_clear()`;
- every SET constant fold or poll-to-jump strengthening emits `js_debug_assert_exception_set()`;
- each helper logs a distinct site id/function name and aborts on mismatch;
- both helpers are audited `NO_GC`, `NO_REENTRY`, and `PRESERVES`.

Release builds contain no tripwire calls. Until E5, the test harness can build both the online path and the legacy blind-emission + offline-pass path. Differential tests compare observable results and normalized MIR budgets; the legacy mode is removed with the offline pass.

---

## 2. Poll-site inventory (E0 conversion table)

`js_check_exception` emission sites at anchor commit (39 total). R = route, G = direct exception-true guard, T = value-producing status test, P = cleanup/saved-exception protocol using T internally.

| Site | Purpose | → |
|---|---|---|
| statement_lowering 5772 / 5778 | try-body per-statement poll (catch / finally) | R |
| statement_lowering 1969 | for-loop back-edge check | R |
| statement_lowering 4292, 4410, 4414, 4420, 4434, 4442, 4449, 4500, 4512, 4520 | for-of / for-await step, LHS assign, destructuring checks | R, G, or P after per-site edge/cleanup audit |
| statement_lowering 4788 | route into `using` cleanup | R |
| statement_lowering 4800 / 4805 | save original exception / prefer new cleanup exception | T / P |
| statement_lowering 5983 / 6022 | save original exception / prefer new finally exception | T / P |
| expression_lowering 848 | `super()` guard before this-binding update | G → convert to R in E4 (§4c) |
| expression_lowering 3572, 3598, 3666, 3698 | module-var / global strict read-write guards | G |
| expression_lowering 3924, 4001, 4052, 4111 | array-destructuring iterator guards | G |
| expression_lowering 4251 | object-destructuring RequireObjectCoercible guard | G |
| expression_lowering 13434 | generator abrupt-completion iterator-close guard | G |
| function_class_lowering 1361, 1869, 3106 | param-destructuring coercible guards | G |
| function_class_lowering 1385 | generator prologue param-destructuring check | R |
| function_class_lowering 2156 | class-body statement loop | R |
| function_class_lowering 3680 | async-body per-statement poll | R |
| iterator.cpp 43, 63 | save/clear/close/re-throw helpers | P |
| completion.cpp 155 | inside `jm_emit_pending_exception_check` itself | (becomes route impl) |
| completion.cpp 581 | `jm_emit_pending_exception_exit` (function exit) | R |

Helper-emitted route sites: generic propagation helper `1646`; await stmt `3807/3836`; await expr `13480/13521`. Explicit throw does **not** use the propagation helper: `jm_emit_throw_completion` already emits `js_throw_value` followed by a direct jump/return.

---

## 3. Phases

Each phase lands independently with `make test-lambda-baseline` green.

### E0 — Chokepoint unification (mechanical, no behavior change)
1. Add `jm_emit_exception_test`, `jm_emit_exception_route` (rename of `jm_emit_pending_exception_check`; keep an inline alias during migration), and `jm_emit_exception_guard`.
2. In E0, T always emits `js_check_exception`; R/G/P preserve each site's existing branch polarity, cleanup, and argument-stack behavior. No tracking or folding is enabled.
3. Convert every raw site per §2. Emission must stay **bit-identical** — verify via the mir-check harness / `test/mir/mir_budgets.json`; no budget change is accepted.
4. Bypass audit:
   - grep raw `"js_check_exception"` calls in JS MIR lowering;
   - grep `mir_new_call_with_args`, raw `MIR_CALL`/`MIR_JCALL`, and `em_emit_unclassified_call`;
   - prove every resolved and unclassified call reaches the exception-effect callback planned for E1.
5. Add semantic tests for saved-exception/finally/iterator-close protocols before optimization.
6. Exit: baseline 100%, protocol tests green, budgets byte-for-byte unchanged.

### E1 — Tracker core, skipping live
1. Add `JsExcTrack` + `mt->exc_track`; initialize UNKNOWN at every function/state-machine entry and save/restore it at every nested compilation boundary.
2. Add `note_call_exception`; invoke it from `em_after_resolved_call` and `em_emit_unclassified_call`; wire JS in `jm_create_mir_transpiler`. Reclassify `js_check_exception` → PRESERVES and `js_clear_exception` → CLEARS.
3. Make `jm_emit` mark all unconditional JMP/RET emission UNREACHABLE, including RET-to-return-label rewriting. `jm_emit_label` sets UNKNOWN; add `jm_emit_label_with_state` with no broad users yet.
4. Enable CLEAN/SET constant folding in T and CLEAN/UNREACHABLE skipping in R/G. UNKNOWN emits exactly the existing test/branch shape. Only R performs argument-stack unwind.
5. Delete the adjacent peephole (a route's fall-through is CLEAN, so the would-be duplicate folds).
6. Land both OE10 tripwires and the temporary legacy differential mode.
7. Add focused lattice tests: every state × every call effect; every state × T/R/G; nested argument scopes; SET followed by MAY_SET must become UNKNOWN.
8. Exit: baseline and differential results green. Budget updates may contain reductions only; unexplained increases fail review.

### E2 — SETS classification + route strengthening
1. Add `JIT_EXCEPTION_SETS`; audit + reclassify per OE4 table.
2. Route/guard helpers implement the SET row of OE5: debug assert set, then unconditional `MIR_JMP` (route also unwinds active argument scopes), state → UNREACHABLE.
3. Explicit `throw x` remains the existing `js_throw_value; jmp/ret`; E1's transfer tracking removes its statically unreachable trailing statement poll. E2 optimizes conditional routing after audited SETS helpers such as const-assignment/named-error paths.
4. Add a negative test helper in debug/test builds that clears the flag before return; verify it cannot be classified SETS and that MAY_SET from SET becomes UNKNOWN.
5. Exit: baseline + error-handling JS suites + differential mode green; record reductions by audited SETS call site rather than assuming every throw changes.

### E3 — Structured merges
1. Merge API plus branch-point state capture/arm-entry restoration; if/else joins; logical/conditional/nullish expression joins.
2. Catch label emitted with state SET (then `js_clear_exception` → CLEAN via chokepoint 1) — polls inside catch bodies before the first may-throw call disappear.
3. Await joins: `after_await_label` in both `jm_emit_await_value_reg` (statement) and the await-expression lowering emitted via `jm_emit_label_with_state(merge(fast, resume))` → CLEAN → the trailing statement poll disappears (§4a).
4. Add merge matrix tests including missing else, short circuit, an UNREACHABLE arm, both arms SET, break/continue conservatism, and generator resume.
5. Exit: baseline + differential mode; budgets may only stay flat or drop.

### E4 — Guard→route conversions
1. `super()` (§4c): reorder to route-first — poll routes to the completion target immediately after the super call; the this-binding update and field initialization run on the CLEAN fall-through. Deletes the private `done`-label guard shape and makes the trailing statement poll skippable.
2. Audit the module-TDZ / global-strict guards (3572/3598/3666/3698) individually. Convert only when "skip local operation and later route" is observably equivalent to immediate routing, including argument-stack and cleanup behavior.
3. Do not convert iterator-close/finally/using protocols; they intentionally execute work while an exception is pending or saved.
4. Exit: baseline + differential mode; targeted side-effect-order tests; budgets.

### E5 — Deletions (OE9)
1. Run the full differential matrix one final time, including debug tripwires, forced GC, generators/async, argument-stack unwind, and cleanup protocols.
2. Delete the offline pass, legacy emission mode, and OE9 fields/plumbing.
3. Exit: baseline green on debug AND release hosts; grep confirms no `is_exception_poll` / `last_exception_poll` / `jm_optimize_exception_polls` references remain.

### E6 — Validation & measurement
- `make test-lambda-baseline` 100% (includes MT7 emission ratchet).
- `make node-baseline` — no regression vs 1492/3517.
- Targeted exception matrix: catch destructuring, finally precedence, `using`, iterator close, nested arg-stack scopes, explicit throw, SETS helpers, generator throw/return, and both await paths.
- JS forced-GC sweep on exception-heavy scripts (arg-stack unwind + rooting interaction).
- Perf: (a) transpile time on the large module batch corpus (jquery/popper) — expect a win from deleting per-function CFG+fixed-point; (b) runtime spot-check: awfy subset + exception-heavy helper-route and explicit-throw microbenchmarks.
- Record results + per-suite budget deltas at the bottom of this doc. Reject unexplained poll-count or MIR-size increases rather than rebaselining them.

---

## 4. Stacked-poll unification (await / throw / super)

**(a) `await f()` in a try** — today: 2 helper polls (fast path after `js_async_must_suspend`; resume path after `jm_emit_async_resume_refresh`) + 1 trailing statement poll = 3 static, 2 executed per dynamic path. The two path polls are **genuinely disjoint** (each execution runs exactly one) and guard different rejection sources — they stay. The trailing statement poll is eliminated by the E3.3 join merge. Net: 3→2 static, 2→1 executed.

**(b) `throw x;`** — today, `jm_emit_throw_completion` already emits `js_throw_value` + unconditional `jmp/ret`; the enclosing try/async/class statement loop may still emit an unreachable trailing poll. After E1, centralized transfer tracking marks the path UNREACHABLE and suppresses that trailing poll. Net: existing direct throw transfer retained; unreachable poll removed. SETS adds value at other helper-followed route sites, not at the explicit throw transfer itself.

**(c) `super()`** — today: guard poll (BT over the this-binding update to a local `done` label) + trailing statement poll that finally routes = 2 polls, and the join at `done` merges unknown+clean so the second poll is unavoidable. After E4: one route poll immediately after the call (taken edge → handler), CLEAN fall-through does the this-binding; trailing statement poll skipped. Net: 2→1, which is optimal (the super call is genuinely may-throw).

General rule extracted from these: **path-disjoint polls cannot merge (await); definitely-set states strengthen a subsequent conditional route to a jump; existing unconditional transfers suppress unreachable trailing polls; skip-guards that rejoin normal flow may become route-first only when cleanup, argument-stack, and observable ordering are unchanged (super and selected TDZ guards)**.

---

## 5. Risks

| Risk | Mitigation |
|---|---|
| Unsound CLEAN → skipped poll → statements execute after a throw | Single status-test chokepoint; label-default-UNKNOWN; clear tripwire; focused lattice tests; differential legacy mode |
| Unsound SET → unconditional jump when the flag was cleared | MAY_SET always produces UNKNOWN; set tripwire; SETS requires an every-return-path proof |
| Finally / iterator-close saved-exception protocol broken by generic guard conversion | OE8 value-producing test + protocol-specific control flow; no `force_emit` shortcut |
| Generator/async resume injects rejection into CLEAN region | resume labels are plain labels → UNKNOWN by default |
| Raw/unclassified call bypasses call-effect tracking | notify from both resolved and unclassified call paths; grep/audit raw MIR call construction |
| `em_root_note_call_site` signature change ripples to lambda/py | mechanical, compiler-checked (E5) |
| Nested function compilation clobbers tracker state | `exc_track` added to every existing function/state-machine save/reset/restore set |
| `jm_emit_label_with_state` overlooks a predecessor | restrict it to owned structured labels; debug predecessor registration/assertions; otherwise use plain UNKNOWN |
| Argument-stack unwind is accidentally added to a local guard | unwind belongs to route only; E0 requires bit-identical MIR |
| MIR cache serves stale emission | bump the relevant cache/version key; verify legacy/online modes cannot share incompatible artifacts |

## 6. Expected outcome

- Emitted polls: target parity with the offline pass for the structured joins implemented in E3, conservative UNKNOWN at unowned labels, and additional reductions at audited SETS routes, explicit-transfer tails, and safe guard→route conversions.
- Compile time: per-function CFG build + fixed point for polls deleted; poll-free-function early-exit becomes moot.
- Code: the pass/peephole/plumbing deletion should outweigh the tracker, status/control helpers, merge support, and debug-only validation. Record the actual line delta after E5 rather than relying on the earlier estimate.

# Lambda Impl Plan — Online Exception-Poll Tracking (JS MIR)

**Date**: 2026-07-23  **Status**: PLANNED  **Tree anchor**: master `f8e454d75` (line refs below are as of this commit; anchor by function name when they drift)

**Decision**: Replace the blind-emit-then-optimize exception-poll scheme with an **online abstract state** consulted at emission time. Polls are skipped (or strengthened to unconditional jumps) directly at emission. The adjacent-poll peephole and the `jm_optimize_exception_polls` dataflow pass are deleted at the end. No shadow/oracle phase — the tracker skips for real from the first landing, guarded by baselines and a debug-only tripwire.

---

## 0. Context

Current mechanism (all from the 2026-07-20 tuning commit `8840e937c` unless noted):

1. **Blind emission**: per-statement polls in try bodies (`js_mir_statement_lowering.cpp:5772/5778`), async bodies (`js_mir_function_class_lowering.cpp:3680`), class-body statement loops (`js_mir_function_class_lowering.cpp:2156`), plus ~30 special-site polls (inventory §2).
2. **Adjacent peephole**: `last_exception_poll_branch/_target` in `jm_emit_pending_exception_check` (`js_mir_completion.cpp:148`) — dedupes only literally-adjacent same-target polls, only for helper-emitted ones.
3. **Offline pass**: `jm_optimize_exception_polls` (`js_mir_completion.cpp:218`, ~310 lines) — per-function CFG + fixed-point cleanliness dataflow + poll deletion, run at function finalization (`js_mir_hashmap_scope_utils.cpp:453`). Allocates ~10 instruction-count-sized arrays per function; already needed a poll-free early-exit because CFG construction "dominates compile time in large module batches".

Costs of the current design: emit-then-delete churn, per-function CFG/fixed-point compile time, two overlapping mechanisms, and the pass cannot exploit *definitely-set* knowledge (a `throw` still emits poll+branch instead of a plain jump).

Key enabling facts (verified 2026-07-23):

- **All import calls flow through one hook**: named calls, void calls, and direct-pointer calls all end in `em_after_resolved_call` (`mir_emitter_shared.hpp:2760/2791/2939`) with the resolved `JitCallMetadata` in hand — including `effects.exception`.
- **All labels flow through one function**: `jm_emit_label` (`js_mir_hashmap_scope_utils.cpp:490`).
- **Effect metadata already exists per helper**: `JitImport` rows in `sys_func_registry.c`; `exception_effect` field defaults to `JIT_EXCEPTION_MAY_SET = 0` (conservative-by-default), some rows explicitly `PRESERVES`. Enum in `sys_func_registry.h:86`.
- The pass assumes **function entry is dirty** (`entry_clean[0] = false`, `js_mir_completion.cpp:413`); the tracker must mirror this.

---

## 1. Design (OE1–OE10)

### OE1 — State

```c
// js_mir_context.hpp
typedef enum JsExcTrack {
    JS_EXC_DIRTY = 0,     // flag may be set (unknown) — conservative default
    JS_EXC_CLEAN,         // flag provably false
    JS_EXC_SET,           // flag provably true (after an unconditional thrower)
    JS_EXC_UNREACHABLE,   // code after an unconditional transfer; next label revives
} JsExcTrack;
// field: JsExcTrack exc_track;   on JsMirTranspiler
```

Function entry state = `DIRTY` (mirrors the pass). `exc_track` is part of the per-function context that is **saved/restored around nested function compilation**, exactly like `func_except_label` is today (`js_mir_function_class_lowering.cpp:733/752`).

### OE2 — Transition rules

| Event | New state |
|---|---|
| function entry / nested-fn entry | DIRTY |
| call with `JIT_EXCEPTION_PRESERVES` | unchanged |
| call with `JIT_EXCEPTION_CLEARS` | CLEAN |
| call with `JIT_EXCEPTION_MAY_SET` | SET stays SET; else DIRTY |
| call with `JIT_EXCEPTION_SETS` (new, OE4) | SET |
| poll emitted (route or guard), fall-through path | CLEAN (explicit overwrite by the poll helper — chokepoint 2 owns the state, so the poll's own `js_check_exception` call effect is irrelevant) |
| `jm_emit_label` (plain) | DIRTY |
| `jm_emit_label_with_state(l, s)` | `s` (construct-supplied merge) |
| after emitting unconditional `MIR_JMP` / `ret` in completion helpers | UNREACHABLE |

Merge for construct-supplied states: `merge(UNREACHABLE, x) = x`; `merge(CLEAN, CLEAN) = CLEAN`; `merge(SET, SET) = SET`; anything else = DIRTY.

### OE3 — Exactly three chokepoints

1. **Call effects**: new `MirEmitter` callback `note_call_exception(void* owner, JitExceptionEffect effect)`, invoked from `em_after_resolved_call`. JS sets it in `jm_create_mir_transpiler`; Lambda/Py leave it NULL.
2. **Poll helpers**: `jm_emit_exception_route()` / `jm_emit_exception_guard()` (OE5) — the only functions allowed to emit `js_check_exception` and the only ones that set CLEAN.
3. **Labels**: `jm_emit_label` / `jm_emit_label_with_state`.

Discipline rule: no lowering code writes `mt->exc_track` directly; only these helpers and the merge API (`jm_exc_*`) touch it. Grep-able: `exc_track` must appear only in `js_mir_completion.cpp`, `js_mir_hashmap_scope_utils.cpp`, and the context header.

### OE4 — New effect class `JIT_EXCEPTION_SETS`

Append to `JitExceptionEffect` (`sys_func_registry.h`). Audit rule: SETS means the helper **unconditionally** leaves the flag set on **every** return path. Initial audited list (verify each body at impl time):

| Helper | New class |
|---|---|
| `js_throw_value` | SETS |
| `js_throw_named_error` | SETS |
| `js_throw_const_assign` | SETS |
| `js_clear_exception` | CLEARS |
| `js_check_exception` | PRESERVES |
| `js_require_object_coercible`, other conditional throwers | stay MAY_SET |

Note: `js_throw_type_error` / `_range_error` / `_code` variants are C-side helpers mostly called from C, not JIT imports — classify only rows that exist in `jit_runtime_imports`. The value flows through the Jube catalog as `uint32_t` (`jube_registry.cpp:1436`) — additive, no ABI break; the (to-be-deleted) pass treats SETS as "not PRESERVES/CLEARS" = dirty, so phases can land in any order safely.

### OE5 — Poll emission semantics

Two helpers in `js_mir_completion.cpp`, replacing every raw site:

- **`jm_emit_exception_route(mt, JsMirCompletionKind kind)`** — today's `jm_emit_pending_exception_check`, renamed to make call sites self-describing. Routes to the completion target (catch / finally / `func_except_label`).
- **`jm_emit_exception_guard(mt, MIR_label_t target)`** — for local skip-guards (destructuring skips, iterator-close checks, module-TDZ writes) where the taken edge goes to a construct-private label.

Both consult `exc_track`:

| State | Route behavior | Guard behavior |
|---|---|---|
| CLEAN | emit **nothing** | emit nothing |
| UNREACHABLE | emit nothing | emit nothing |
| DIRTY | `call js_check_exception; BT target` (+ arg-stack unwind variant when `arg_stack_scope->mark` active, unchanged from today); fall-through state → CLEAN | same, fall-through → CLEAN |
| SET | `js_args_restore` if in arg scope, then **unconditional `MIR_JMP target`**; state → UNREACHABLE | same |

The arg-stack unwind block currently inside `jm_emit_pending_exception_check` (`js_mir_completion.cpp:157–177`) is preserved verbatim for both DIRTY and SET emission.

### OE6 — Label rule

`jm_emit_label`: `exc_track = DIRTY` (any label may be a join from an unknown predecessor — generator resume states, break/continue targets, exception edges). New `jm_emit_label_with_state(mt, label, state)` for constructs that own **all** predecessors of a label and can supply the true merge (OE7). UNREACHABLE never survives a label.

### OE7 — Structured merges (v1 scope)

Merge API: `JsExcTrack jm_exc_state(mt)`, `void jm_exc_set_state(mt, s)` (helper-internal), `JsExcTrack jm_exc_merge(a, b)`.

| Construct | Treatment |
|---|---|
| if/else join | save state after then-arm, lower else-arm, emit join via `jm_emit_label_with_state(merge(then, else))` |
| `&&` / `||` / `?:` / `??` joins | same pattern at expression joins |
| catch label | `jm_emit_label_with_state(catch_label, JS_EXC_SET)` — entered only via exception edges; the `js_clear_exception` call at catch entry then flips CLEAN automatically via chokepoint 1 |
| await internal join (`after_await_label`) | merge(fast-path, resume-path) — both CLEAN after their polls → CLEAN (§4a) |
| loop head labels | plain label → DIRTY (conservative; body unknown at head-emission time) |
| loop exit / break targets | v1: plain label → DIRTY. Optional later: merge of registered break-site states |
| finally label | plain label → DIRTY (reached from normal + exceptional edges; saved-exc protocol does its own flag surgery) |
| generator/async resume labels (`gen_state_labels`) | plain label → DIRTY (resumption may inject a rejection) |

### OE8 — Saved-exception protocol sites are exempt from skipping

The finally routing and iterator-close sequences that **save, clear, run cleanup, re-throw** (`js_mir_statement_lowering.cpp:4788–4805, 5983, 6022`) read the flag as *data*, not as a route decision. Convert them to the guard helper for chokepoint hygiene but mark them `force_emit` (a bool param) in v1 — they run in states the tracker calls DIRTY anyway, so this costs nothing today; revisit only if a profile says otherwise.

### OE9 — Deletions (final phase)

- peephole fields + logic: `last_exception_poll_branch/_target` (`js_mir_context.hpp:445`, `js_mir_completion.cpp:148–154`, reset in `js_mir_hashmap_scope_utils.cpp:358`)
- `jm_optimize_exception_polls` + `jm_exception_poll_result` + `jm_branch_uses_poll_result` + `jm_merge_exception_predecessor` + the call at `js_mir_hashmap_scope_utils.cpp:453`
- `MirGcCallSite.is_exception_poll` field, the `strcmp(call_name, "js_check_exception")` classification (`mir_emitter_shared.hpp:2611–2615`), and the extra `em_root_note_call_site` parameter (touches lambda + py signatures, mechanical)
- **Keep** the `em_root_*` CFG utilities — root write-back liveness (`jm_finalize_write_back_roots`) still uses them.

### OE10 — Debug tripwire (debug builds only, zero release cost)

Where a route/guard is **skipped because CLEAN**, debug builds emit `call js_debug_assert_exception_clear()` — a new 4-line helper that `log_error`s and aborts if the flag is set. Compiled under `#ifndef NDEBUG` (same convention as existing debug asserts). This is not an oracle phase — skipping is live from E1 — it is a tripwire that turns any unsound-CLEAN bug into an immediate, located failure in `make test-lambda-baseline` instead of a silent semantic divergence.

---

## 2. Poll-site inventory (E0 conversion table)

`js_check_exception` emission sites at anchor commit (39 total). R = becomes `jm_emit_exception_route`, G = `jm_emit_exception_guard`, G* = guard with `force_emit` (OE8).

| Site | Purpose | → |
|---|---|---|
| statement_lowering 5772 / 5778 | try-body per-statement poll (catch / finally) | R |
| statement_lowering 1969 | for-loop back-edge check | R |
| statement_lowering 4292, 4410, 4414, 4420, 4434, 4442, 4449, 4500, 4512, 4520 | for-of / for-await step, LHS assign, destructuring checks | R or G (per-site: routes to `l_iter_exc` cleanup = G with cleanup label) |
| statement_lowering 4788, 4800, 4805 | iterator-close save/clear/re-throw protocol | G* |
| statement_lowering 5983, 6022 | finally saved-exc routing | G* |
| expression_lowering 848 | `super()` guard before this-binding update | G → convert to R in E4 (§4c) |
| expression_lowering 3572, 3598, 3666, 3698 | module-var / global strict read-write guards | G |
| expression_lowering 3924, 4001, 4052, 4111 | array-destructuring iterator guards | G |
| expression_lowering 4251 | object-destructuring RequireObjectCoercible guard | G |
| expression_lowering 13434 | generator abrupt-completion iterator-close guard | G |
| function_class_lowering 1361, 1869, 3106 | param-destructuring coercible guards | G |
| function_class_lowering 1385 | generator prologue param-destructuring check | R |
| function_class_lowering 2156 | class-body statement loop | R |
| function_class_lowering 3680 | async-body per-statement poll | R |
| iterator.cpp 43, 63 | `jm_emit_iterator_close_on_exception` helpers | G |
| completion.cpp 155 | inside `jm_emit_pending_exception_check` itself | (becomes route impl) |
| completion.cpp 581 | `jm_emit_pending_exception_exit` (function exit) | R |

Helper-emitted sites (already routed): throw stmt `1646`; await stmt `3807/3836`; await expr `13480/13521`.

---

## 3. Phases

Each phase lands independently with `make test-lambda-baseline` green.

### E0 — Chokepoint unification (mechanical, no behavior change)
1. Add `jm_emit_exception_route` (rename of `jm_emit_pending_exception_check`; keep an inline alias during migration) and `jm_emit_exception_guard(mt, target, force_emit)` — both emit unconditionally in this phase.
2. Convert all raw sites per §2 table. Emission should stay **bit-identical** — verify via the mir-check harness / `test/mir/mir_budgets.json` (no budget change expected).
3. Bypass audit: confirm no JS lowering emits calls outside the `em_*` call paths (verified at plan time: all three call paths hit `em_after_resolved_call`; re-grep `mir_new_call_with_args` in `lambda/js/` to be sure).
4. Exit: baseline 100%, budgets unchanged.

### E1 — Tracker core, skipping live
1. Add `JsExcTrack` + `mt->exc_track`; init DIRTY at function begin; add to the nested-function save/restore set alongside `func_except_label`.
2. Add `note_call_exception` callback to `MirEmitter`; invoke from `em_after_resolved_call`; wire in `jm_create_mir_transpiler`. Reclassify `js_check_exception` → PRESERVES and `js_clear_exception` → CLEARS in `sys_func_registry.c` (hygiene; the poll helper overwrites state anyway).
3. `jm_emit_label` sets DIRTY; add `jm_emit_label_with_state` (no users yet).
4. Enable skip logic (OE5) for CLEAN/UNREACHABLE in both helpers; DIRTY emits as today with fall-through → CLEAN. UNREACHABLE is set only by completion helpers that end in unconditional JMP/ret (`jm_emit_throw_completion`, `jm_emit_break/continue_completion`, return completions).
5. Delete the adjacent peephole (subsumed: after a poll, state is CLEAN, so the would-be duplicate is skipped).
6. OE10 tripwire lands here.
7. Exit: baseline 100%; re-baseline `mir_budgets.json` (poll counts drop — reductions are the goal; the MT7 ratchet then locks them).

### E2 — SETS classification + throw-to-jump
1. Add `JIT_EXCEPTION_SETS`; audit + reclassify per OE4 table.
2. Route/guard helpers implement the SET row of OE5: unconditional `MIR_JMP` (+ arg unwind), state → UNREACHABLE. `throw x;` now lowers to `call js_throw_value; jmp <handler>` with **zero** polls; the try-loop poll after it is skipped as UNREACHABLE.
3. Exit: baseline + error-handling JS suites green; budgets drop at every throw site.

### E3 — Structured merges
1. Merge API; if/else joins; logical/conditional expression joins.
2. Catch label emitted with state SET (then `js_clear_exception` → CLEAN via chokepoint 1) — polls inside catch bodies before the first may-throw call disappear.
3. Await joins: `after_await_label` in both `jm_emit_await_value_reg` (statement) and the await-expression lowering emitted via `jm_emit_label_with_state(merge(fast, resume))` → CLEAN → the trailing statement poll disappears (§4a).
4. Exit: baseline; budgets drop further (measure and record the delta per suite).

### E4 — Guard→route conversions
1. `super()` (§4c): reorder to route-first — poll routes to the completion target immediately after the super call; the this-binding update and field initialization run on the CLEAN fall-through. Deletes the private `done`-label guard shape and makes the trailing statement poll skippable.
2. Audit the module-TDZ / global-strict guards (3572/3598/3666/3698) for the same conversion where the fall-through continues normal flow (per-site semantic check: current behavior "skip the write, continue" must equal "route to handler" — true whenever a statement-level route follows with no intervening observable effect).
3. Exit: baseline; budgets.

### E5 — Deletions (OE9)
Exit: baseline green on debug AND release hosts (the mir-check harness runs on both); grep confirms no `is_exception_poll` / `last_exception_poll` / `jm_optimize_exception_polls` references remain.

### E6 — Validation & measurement
- `make test-lambda-baseline` 100% (includes MT7 emission ratchet).
- `make node-baseline` — no regression vs 1492/3517.
- JS forced-GC spot sweep on exception-heavy scripts (arg-stack unwind + rooting interaction).
- Perf: (a) transpile time on the large module batch corpus (jquery/popper) — expect a win from deleting per-function CFG+fixed-point; (b) runtime spot-check: awfy subset + a try/throw microbenchmark (throw path is now jump-based).
- Record results + budget deltas at the bottom of this doc.

---

## 4. Stacked-poll unification (await / throw / super)

**(a) `await f()` in a try** — today: 2 helper polls (fast path after `js_async_must_suspend`; resume path after `jm_emit_async_resume_refresh`) + 1 trailing statement poll = 3 static, 2 executed per dynamic path. The two path polls are **genuinely disjoint** (each execution runs exactly one) and guard different rejection sources — they stay. The trailing statement poll is eliminated by the E3.3 join merge. Net: 3→2 static, 2→1 executed.

**(b) `throw x;`** — today: `js_throw_value` + poll + BT (+ trailing statement poll). After E2: `js_throw_value` + unconditional `jmp`; everything after is UNREACHABLE-skipped. Net: 2→0 polls. This is a strict improvement over the offline pass, which can only delete provably-*clean* polls and cannot exploit provably-*set* state.

**(c) `super()`** — today: guard poll (BT over the this-binding update to a local `done` label) + trailing statement poll that finally routes = 2 polls, and the join at `done` merges dirty+clean so the second poll is unavoidable. After E4: one route poll immediately after the call (taken edge → handler), CLEAN fall-through does the this-binding; trailing statement poll skipped. Net: 2→1, which is optimal (the super call is genuinely may-throw).

General rule extracted from these: **path-disjoint polls can't merge (await); definitely-set states convert polls to jumps (throw); skip-guards that rejoin normal flow should become route-first so the fall-through stays CLEAN (super, TDZ guards)**.

---

## 5. Risks

| Risk | Mitigation |
|---|---|
| Unsound CLEAN → skipped poll → statements execute after a throw (silent) | OE3 chokepoint discipline (3 sites, grep-able); label-default-DIRTY; SETS audit = unconditional-set proof per helper; OE10 debug tripwire; full JS baselines + node-baseline |
| Finally / iterator-close saved-exc protocol broken by skipping | OE8 `force_emit` exemption in v1 |
| Generator/async resume injects rejection into CLEAN region | resume labels are plain labels → DIRTY by default |
| Helper called while flag already SET (poisoned segment) mis-modeled | MAY_SET from SET stays SET (OE2); poisoning semantics unchanged |
| `em_root_note_call_site` signature change ripples to lambda/py | mechanical, compiler-checked (E5) |
| Nested function compilation clobbers tracker state | `exc_track` added to the existing save/restore set (E1.1) |
| MIR cache serves stale emission | cache stores post-emission artifacts; budgets/sidecars re-baselined per phase |

## 6. Expected outcome

- Emitted polls: parity with the offline pass on join-heavy code (E3), strictly better on throw (E2) and super/guards (E4).
- Compile time: per-function CFG build + fixed point for polls deleted; poll-free-function early-exit becomes moot.
- Code: −~450 lines (pass + peephole + plumbing) vs +~150 (tracker + merge API + helpers).

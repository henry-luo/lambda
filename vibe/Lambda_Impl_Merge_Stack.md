# Implementation Plan: JS Runtime Stack Merges

**Status:** planned (2026-07-24). Not started.

**Design authority:** `Lambda_Design_Stack_API.md` §17 (decision record) and §8
(frame watermark discipline); Appendix A (stack inventory + audit findings).
Related: `Lambda_Design_Stack_Rooting.md` (safepoint-current canonical slots),
`Lambda_Impl_MIR_Emission_Test.md` (MT7 emission ratchet, affected by Merge A
Phase 4 only).

**Scope:** LambdaJS MIR-direct lowering and JS runtime only. The Lambda-side
transpiler never used `js_args_*`. C2MIR is frozen and out of scope.

This plan covers three independent merges, ordered by decision date:

- **Merge A — call-argument stack → side-root region.** `js_args_stack` is a
  hand-rolled duplicate of the side-root stack; its frames move onto the
  per-Context side-root region. A *lifetime* merge.
- **Merge B — shared rooted context-stack helper.** The runtime hand-rolls
  six-plus copies of "small fixed global `Item` stack + depth + batch reset +
  GC root registration", and the registration is applied inconsistently —
  which is exactly how the confirmed `js_with_stack` use-after-free happened
  (Appendix A audit, 2026-07-24). One `JsContextStack` helper with
  **non-optional** registration replaces the boilerplate. A *mechanism*
  merge; each stack keeps its own storage and semantics.
- **Merge C — eval-frame consolidation.** `js_eval_source_*` (5 parallel
  arrays) and the 6 eval binding-journal frame-index stacks all bracket the
  same eval invocation, with the pop discipline duplicated across ~25
  early-return branches of `js_mir_eval_lowering.cpp`. They collapse into one
  `JsEvalFrame` stack with a single push/pop. A *data* merge.

Dependency order: A is independent. C builds on B's helper for its `Item`
members, so land B before C. B should land after the standalone
`js_with_stack` rooting fix (task_b9e61cf7) and absorb it.

---

# Merge A: call-argument stack → side-root region

**Non-goals:** `js_with_stack` (semantic `with` scope chain — stays separate),
the number region (non-scanned raw payloads — untouched), the generator/async
argument spill path (`js_alloc_env` heap envs — untouched).

**Clean-removal requirement (hard rule for all three merges):** whatever a
merge replaces must be **completely removed**, not bypassed. No dead buffer,
no orphaned declarations, no empty compatibility stubs, no `#if 0` remnants,
no stale comments describing the old model may survive a merge's final phase.
For Merge A that means the private argument stack and its lifecycle (deletion
checklist in A's Phase 5, enforced by A's gate 4; Phase 2's temporarily
emptied lifecycle bodies are the only sanctioned transitional stubs and
Phase 3 deletes them). For Merge B it means each migrated stack's bespoke
registration/reset boilerplate goes in the same commit as its migration. For
Merge C it means the seven retired stacks, their depth counters, and their
individual push/pop entry points (C3). A merge that leaves any of these
behind is incomplete regardless of test results.

---

## 1. Current state

### 1.1 The private argument stack

`lambda/js/js_runtime_function.cpp` (~lines 140–215):

- `JS_ARGS_STACK_CAP` = 256 K Items (2 MB), `mem_calloc`'d once, never freed
  until `js_args_stack_cleanup()`.
- The **whole capacity** is registered once as a GC root range via
  `heap_register_gc_root_range` — so the collector scans all 256 K slots every
  collection, and the code must maintain a `[len, cap)` zeroed invariant.
- `js_args_push(count)` — bump-allocates `count` pre-zeroed slots; on
  overflow falls back to `js_alloc_env(count)` (a GC-traced heap env).
- `js_args_save()` — returns the current length as an `int64_t` mark.
- `js_args_restore(mark)` — re-zeroes `[mark, len)` (memset) and pops.
  Guard: no-op when `mark >= len`.
- `js_args_stack_reset()` — batch-boundary hook: clears live slots, drops the
  GC registration (heap may be recreated), called from
  `js_reset_transient_call_state()` (`lambda/js/js_runtime_state.cpp:1232`).
- `js_args_stack_cleanup()` — process-exit hook (`lambda/main.cpp:440`).

Declarations: `lambda/js/js_runtime.h:232–236`.

### 1.2 JIT lowering protocol (unchanged by Phases 1–3)

- `JsMirArgStackScope` (`lambda/js/js_mir_context.hpp:327`; active pointer at
  `:459`, reset at function start in
  `lambda/js/js_mir_hashmap_scope_utils.cpp:366`).
- Every `CALL_EXPRESSION` / `NEW_EXPRESSION` opens/closes a scope
  (`lambda/js/js_mir_expression_lowering.cpp:13259–13271`, call case at
  `:13285`, new case at `:13334`). `jm_end_arg_stack_scope` emits
  `js_args_restore(mark)` when the scope has a mark.
- The mark is emitted **lazily at first push**:
  `lambda/js/js_mir_function_collection_class_inference.cpp:3162` emits
  `js_args_save`, then `:3164` emits `js_args_push`. Zero-argument and
  spill-path calls carry no argument-stack protocol at all.
- Exceptional edges restore to the **oldest** enclosing mark
  (`jm_oldest_arg_stack_mark`, `lambda/js/js_mir_completion.cpp:210`;
  restore emission at `:248–252` and `:262–275`) because nested argument
  frames can be half-built when a callee throws.
- Generator/async args containing a suspend point bypass this stack entirely
  and spill to env slots + `js_alloc_env`
  (`js_mir_function_collection_class_inference.cpp:3099–3146`).

### 1.3 The side-root region (merge target)

`lambda/runtime/side_stack.c` / `side_stack.h`:

- Demand-paged reservation, `LAMBDA_SIDE_ROOT_RESERVE_BYTES` = 16 MB
  (2 M slots). Bound to `Context` fields `side_root_base/top/commit_limit/limit`
  by `lambda_side_stack_bind`; growth via `lambda_side_stack_ensure`.
- The GC scans **exactly** `[side_root_base, side_root_top)`
  (`lambda/runtime/lambda-mem.cpp:356–362`) — nothing above the top is ever
  read, so popping requires no re-zeroing.
- `lambda_root_frame_begin` zeroes slots **before** publishing the new top
  (a collection during frame construction must not see stale words as roots).
- `LambdaRecoveryCheckpoint` snapshot/restore already rewinds
  `side_root_top` + `side_number_top` across setjmp/longjmp recovery.
- `lambda_side_stack_decommit_unused` returns committed pages above the tops
  after each collection.
- Generated JS frames already reserve their per-frame root slots by bumping
  `side_root_top` in the prologue and restore it in the epilogue
  (`em_finalize_frame_prologue` / `em_store_frame_top` with
  `offsetof(Context, side_root_top)`).

### 1.4 Lifetime facts that make the merge safe

- **Args never outlive the call expression.** `js_build_arguments_object`
  (`lambda/js/js_runtime_state.cpp:1330`) *copies* `js_pending_call_args`
  into a fresh array at callee entry; the alias is dead before any restore.
- **LIFO interleaving holds.** Layout during a nested call is
  `caller frame roots → caller's arg frame → callee frame roots → …`; the
  callee epilogue restores to *its own* base (above the caller's args), then
  the caller's scope restore pops the args. Strictly nested.
- **The base never moves.** The old code couldn't realloc because a
  partially-filled frame must stay rooted in place while later arguments nest
  more pushes. The side-root reservation gives the same guarantee natively:
  addresses are stable, only the top moves.

---

## 2. Target design

One new runtime primitive plus a reimplementation of the four `js_args_*`
entry points on top of it. The JIT-facing names and C-call ABI are preserved
through Phase 3 so no emitter changes; Phase 4 optionally inlines
save/restore.

```c
// side_stack.h — root-side twin of lambda_side_number_alloc().
// Reserves and ZEROES n contiguous slots above side_root_top, publishes the
// new top, returns the old top. NULL on reserve/ensure failure.
uint64_t* lambda_side_root_alloc_n(Context* context, size_t slot_count);
```

Semantics changes (all invisible to the JIT):

| Aspect | Before | After |
|---|---|---|
| Storage | private 2 MB calloc | side-root reservation slots |
| GC visibility | full-capacity root-range registration | scanned as part of `[base, top)`; no registration |
| Zeroing | pop-time memset (maintains `[len, cap)` invariant) | push-time zeroing before top publish (same rule as `lambda_root_frame_begin`) |
| Mark | stack length (`int64_t`) | saved `side_root_top` pointer (as `int64_t`) |
| Overflow | fallback to `js_alloc_env` | unchanged: fallback to `js_alloc_env` (see §4.3) |
| Batch reset | explicit memset + drop registration | nothing — `lambda_side_stack_reset` / recovery checkpoints already own the watermark |
| Process cleanup | `mem_free` | nothing — region is process-lifetime |
| Post-GC memory | 2 MB resident forever | decommitted with the rest of the region |

A bonus correctness property: any argument frame that an unwind path fails to
pop explicitly is now truncated by the enclosing generated epilogue's
`side_root_top` restore (or by `LambdaRecoveryCheckpoint` on non-local
recovery). Today such a miss leaks private-stack slots until batch reset.

---

## 3. Phases

### Phase 0 — baseline

- `make build-test`, `make test-lambda-baseline` green (record counts; JS
  suites are part of the baseline).
- Confirm the MIR emission fixtures are green so Phase 4's budget impact is
  attributable (`test/mir/mir_budgets.json`, MT7 ratchet).

### Phase 1 — runtime primitive

`lambda/runtime/side_stack.c` / `side_stack.h`:

1. Add `lambda_side_root_alloc_n(Context*, size_t)`:
   - bind-on-demand like `lambda_side_number_alloc` (call
     `lambda_side_stack_ensure(ctx, n, 0)`);
   - zero the `n` slots, **then** advance `side_root_top` — comment must state
     the ordering reason (collection during argument evaluation must never
     scan stale words as roots);
   - return the pre-bump top; NULL on failure.
2. No callers yet; unit-test via a small GTest in the existing side-stack /
   rooting test file (push N, force `heap_gc_collect`, verify no crash and
   that slots are scanned; verify LIFO restore interop with
   `lambda_root_frame_begin/end` frames above it).

Gate: `make build-test` + new unit tests green.

### Phase 2 — reimplement `js_args_*` (behavior parity, zero JIT changes)

`lambda/js/js_runtime_function.cpp`:

1. Delete `JS_ARGS_STACK_CAP`, `js_args_stack`, `js_args_len`,
   `js_args_registered`.
2. `js_args_push(count)`:
   ```
   p = lambda_side_root_alloc_n(context, count)
   if (!p) return js_alloc_env(count);   // parity fallback, see §4.3
   return (Item*)p;
   ```
3. `js_args_save()` → `return (int64_t)(uintptr_t)context->side_root_top;`
   (keep `AutoAssertNoGC`).
4. `js_args_restore(mark)`:
   - guard: no-op unless
     `side_root_base <= (uint64_t*)mark < side_root_top` (mirrors today's
     `m >= len` staleness guard and protects the fallback-frame case where no
     push bumped the top);
   - `context->side_root_top = (uint64_t*)mark;` — **no memset** (GC never
     reads above top). Under `!NDEBUG`, optionally poison popped words with
     a non-pointer pattern to catch retained aliases deterministically
     (match the `0xA5` convention of `lambda_restore_number_frame_top`).
5. `js_args_stack_reset()` / `js_args_stack_cleanup()` → empty bodies for
   this phase (removed in Phase 3).
6. Update the block comment at the top: the "base never moves / cannot
   realloc" rationale now cites the reservation model.

Gate: full `make test-lambda-baseline` (must hold the Phase 0 count) plus a
targeted forced-GC run over call-heavy JS scripts (deep nested call
expressions, callee-throws-mid-arglist cases — the exception-edge restore in
`js_mir_completion.cpp` is the risky path). MIR fixtures must be
**bit-identical**: Phase 2 changes no emission.

### Phase 3 — delete the private lifecycle

1. Remove the `js_args_stack_reset()` call from
   `js_reset_transient_call_state()` (`js_runtime_state.cpp:1232`) and the
   `js_args_stack_cleanup()` call from `lambda/main.cpp:440`; delete both
   functions and their declarations (`js_runtime.h:235–236`).
   - Verify the batch-reset path already restores the side-root watermark at
     that boundary (it does: batch teardown goes through
     `lambda_side_stack_reset` / recovery-checkpoint restore; confirm on the
     actual reset path before deleting, and if a JS batch boundary can be
     reached with a non-base `side_root_top`, keep a one-line
     `side_root_top = <snapshot>` restore at that boundary instead of the old
     memset machinery).
2. Grep for any remaining `heap_register_gc_root_range` special-casing tied
   to the old buffer — none expected beyond the deleted site.

Gate: baseline green; run the batch/multi-test harness specifically (heap
teardown + recreation across tests is what `js_args_stack_reset` existed
for).

### Phase 4 (optional, measured) — inline save/restore in the JIT

Marks are now just `side_root_top` values, so the two C calls per
argument-carrying call expression can become direct `Context` field ops using
the frame's runtime register (`mt->em.frame.runtime`, same pattern as
`em_store_frame_top` with `offsetof(Context, side_root_top)`):

- save (`js_mir_function_collection_class_inference.cpp:3162`) → MIR load of
  `context->side_root_top` into the mark reg;
- restore (`js_mir_expression_lowering.cpp:13267`,
  `js_mir_completion.cpp:250` and `:271`) → MIR store of the mark reg to
  `context->side_root_top`. A plain store is sound: at every restore point
  `top >= mark` (pushes only grow the top; nested scopes and callee epilogues
  restore to watermarks `>= mark`). Keep the C call under `!NDEBUG` if the
  bounds-check diagnostic is wanted in debug builds.
- `js_args_push` stays a C call (it needs ensure/commit growth + zeroing).

Costs to plan for:

- **MT7 emission budgets**: instruction/call counts change in
  `test/mir/mir_budgets.json` fixtures — manual budget lifts required, with
  the delta recorded in the commit message.
- Bench before/after on call-heavy Result-suite scripts to confirm the win
  justifies the budget churn; if not measurable, skip this phase entirely.

### Phase 5 — clean removal audit and records

This phase is a **removal audit**, not optional polish. Every artifact of the
old private stack must be gone. Deletion checklist (each line verified by
grep before sign-off):

| Artifact | Location | Action |
|---|---|---|
| `JS_ARGS_STACK_CAP` macro | `js_runtime_function.cpp` | deleted (Phase 2) |
| `js_args_stack` / `js_args_len` / `js_args_registered` statics | `js_runtime_function.cpp` | deleted (Phase 2) |
| `heap_register_gc_root_range` call for the buffer | `js_runtime_function.cpp:161` | deleted (Phase 2) |
| pop-time memset / `[len, cap)` zeroed-invariant code + comments | `js_runtime_function.cpp` | deleted (Phase 2) |
| `js_args_stack_reset()` body + declaration | `js_runtime_function.cpp`, `js_runtime.h:235` | deleted (Phase 3) |
| `js_args_stack_cleanup()` body + declaration | `js_runtime_function.cpp`, `js_runtime.h:236` | deleted (Phase 3) |
| reset call site | `js_runtime_state.cpp:1232` + its comment | deleted (Phase 3) |
| cleanup call site | `lambda/main.cpp:440` + its comment | deleted (Phase 3) |
| old-model comment block ("registered with the GC once…") | `js_mir_function_collection_class_inference.cpp:3147` | rewritten for the side-root frame |
| "base never moves / cannot realloc" block comment | `js_runtime_function.cpp:~140` | rewritten to cite the reservation model |
| any MIR-import/registry entry for `js_args_stack_reset`/`_cleanup` | JIT import tables, if present | deleted |

Verification commands (all must come back clean):

```
grep -rn "JS_ARGS_STACK_CAP\|js_args_stack\b\|js_args_len\|js_args_registered" lambda/
grep -rn "js_args_stack_reset\|js_args_stack_cleanup" lambda/
```

Only `js_args_push` / `js_args_save` / `js_args_restore` survive — as thin
wrappers over the side-root primitives (or fewer, if Phase 4 inlined
save/restore, in which case delete the orphaned C entry points and their
JIT import registrations too).

Records:

- Update `Lambda_Design_Stack_API.md` §17 status to implemented, with the
  execution record (dates, baseline counts, budget lifts if Phase 4 ran) and
  a note that the removal checklist above completed empty.

---

## 4. Invariants and hazards

### 4.1 Invariants preserved

- **LIFO watermark nesting** with generated frames (§1.4). Functions with
  `root_slot_count == 0` (zero-root numeric functions) never touch
  `side_root_top`, so they neither help nor harm the discipline — explicit
  scope restores still run.
- **Push-before-fill rooting**: slots are zeroed and published before the JIT
  stores evaluated args one by one; a GC triggered while evaluating arg *i+1*
  sees args `0..i` live and the rest as null. Identical to today.
- **`lambda_root_frame_end` non-LIFO detection** still applies to native
  helper frames; argument frames pushed and popped by generated code between
  a native frame's begin/end are invisible to it (top returns to the same
  value).
- **Exception edges** keep restoring to the *oldest* mark
  (`jm_oldest_arg_stack_mark`) — unchanged emission through Phase 3.

### 4.2 Hazards

- **Restore-guard semantics.** The old guard (`mark >= len` → no-op) silently
  tolerated a stale mark and the overflow-fallback case. The new pointer
  guard must tolerate the same two situations: (a) an enclosing restore
  already rewound below the mark (store would move the top *up* — must
  no-op); (b) the scope's only push went to the `js_alloc_env` fallback so
  the top never moved. Get this guard wrong and either roots are resurrected
  (a) or valid pops are skipped.
- **Capacity coupling.** Argument frames now share the 16 MB root region with
  per-frame root slots. Worst-case simultaneous demand ≈ old 2 MB args cap +
  existing frame usage — comfortably inside 2 M slots, and reservation is
  address space, not memory. If a stress suite disproves this, bump
  `LAMBDA_SIDE_ROOT_RESERVE_BYTES`; do not reintroduce a second stack.
- **Decommit interaction.** `lambda_side_stack_decommit_unused` (post-GC)
  releases pages above the top; a subsequent push re-touches them
  (demand-paged). No action needed — just don't "optimize" the zeroing away
  on the assumption pages are fresh: recommitted pages on POSIX
  (`MADV_DONTNEED`) are zero, but slots below `commit_limit` are not.
- **Threading.** Both the old buffer and the side regions are process-global
  statics bound to one `Context`; the merge neither improves nor regresses
  the JS-threading (JT) story. Note it in the JT globals ledger
  (`Lambda_Js_Thread.md` §6.5) so the region shows up in the worker audit.
- **Parallel baseline flakiness.** `test-lambda-baseline` is known to flake
  1–2 heavy tests under parallel load; re-verify any failure standalone
  before attributing it to this change.

### 4.3 Overflow fallback decision

Keep the `js_alloc_env` fallback in Phase 2 for strict behavior parity: the
fallback frame lives on the GC heap, is traced via its env header, is never
"popped" (unreferenced → collected), and the restore guard makes the
scope-end restore a no-op for it — all exactly as today. Converting overflow
to a hard `lambda_stack_overflow_error` (uniform with frame-root overflow)
is a separate one-line follow-up once the merge has soaked; don't couple the
two changes.

---

## 5. Acceptance gates

1. Phases 1–3: `make test-lambda-baseline` at the Phase 0 count, MIR
   emission fixtures bit-identical, forced-GC sweep over call-heavy +
   throw-in-arglist JS scripts clean.
2. Phase 3 additionally: multi-test batch harness green (heap
   teardown/recreation across tests).
3. Phase 4 (if taken): budgets lifted deliberately, bench delta recorded;
   otherwise explicitly record "Phase 4 skipped — not measurable".
4. **Clean removal verified**: the Phase 5 deletion checklist is empty — the
   buffer, cap macro, length/registration statics, root-range registration,
   pop-time memset, both lifecycle functions, their declarations, their call
   sites, and all old-model comments are gone. The grep commands in Phase 5
   return no hits under `lambda/`; no transitional stub, dead branch, or
   `#if 0` block remains. A merge that leaves any of these behind is
   incomplete regardless of test results.

---

# Merge B: shared rooted context-stack helper (`JsContextStack`)

## B.1 Problem

Six-plus semantic context stacks hand-roll the same mechanical shape with
inconsistent GC rooting (Appendix A of `Lambda_Design_Stack_API.md`):

| Stack | Location | Rooted today? |
|---|---|---|
| `js_domain_stack` (64) | `js_runtime.cpp:~30426` | yes — once, `:30605` |
| `js_cjs_module_stack` | `js_mir_entrypoints_require.cpp:~1622` | yes — once, `:1632` |
| `js_eval_source_*` (16 × 5 arrays) | `js_runtime_state.cpp:10–23` | yes — epoch-guarded lazy |
| `js_with_stack` (16) | `js_globals.cpp:~16288` | **no — confirmed UAF** (task_b9e61cf7 fixes standalone) |
| `super_this_*` (128 pairs) | `js_runtime_state.hpp:65–67` | no — safe only because super lowering also writes the rooted lexical-`this` env binding |
| eval binding journals (6 pairs) | `js_globals.cpp:~17118` | no — read path probe passed; restore path unverified |

The bug class is structural: every new stack must remember registration,
epoch handling (heaps are torn down/recreated across batch tests), pop-time
slot nulling, and a batch-reset hook — and one of six forgot. Per CLAUDE.md
rule 13, the shared shape gets extracted.

## B.2 Target design

One helper owning the boilerplate, not the semantics:

```c
// js_runtime_state.hpp — storage stays owned by each client (static arrays).
typedef struct JsContextStack {
    Item*      slots;        // client-owned fixed array
    int        capacity;
    int        depth;
    uint64_t   roots_epoch;  // lazy re-registration after heap recreation
    const char* name;        // diagnostics ("with", "domain", ...)
} JsContextStack;

bool js_context_stack_push(JsContextStack* s, Item value);   // registers on first use per epoch
Item js_context_stack_top(const JsContextStack* s);
void js_context_stack_pop(JsContextStack* s);                // nulls the popped slot
int  js_context_stack_save_depth(const JsContextStack* s);
void js_context_stack_restore_depth(JsContextStack* s, int depth); // nulls [depth, old)
void js_context_stack_reset(JsContextStack* s);              // batch boundary: null + depth 0 + drop epoch
```

Rules:

- **Registration is non-optional and inside the helper** — the
  `js_eval_source_register_roots` epoch pattern
  (`js_runtime_state.cpp:18–23`) becomes the one implementation. A client
  cannot construct an unrooted stack.
- Pop/restore **null vacated slots** (the arrays are always-scanned root
  ranges, so stale entries above depth would be live roots).
- Non-`Item` companion data (line/col offsets, bool flags, watermark ints)
  stays in client-owned parallel arrays indexed by the same depth; the helper
  never registers those.
- All instances go into one static registry table so the batch reset walks
  them uniformly instead of today's scattered reset calls
  (`js_with_batch_reset`, super memsets in `js_reset_transient_call_state`,
  cjs reset, …).

## B.3 Phases

- **B0** — land after task_b9e61cf7 (standalone with-stack fix) is merged;
  its regression test becomes Merge B's canary.
- **B1** — implement `JsContextStack` + unit test (push/pop/restore nulling,
  epoch re-registration across simulated heap recreation, forced-GC scan).
- **B2** — migrate one stack per commit, deleting that stack's bespoke
  boilerplate in the same commit (clean-removal rule of this plan applies:
  no stack keeps a private registration/reset path after migrating):
  1. `js_with_stack` (absorbs the task_b9e61cf7 registration; also move
     `js_last_with_binding_scope/key` cache Items to registered slots),
  2. `js_domain_stack`,
  3. `js_cjs_module_stack`,
  4. `super_this_value_stack` (fold the parallel `bool` array into a
     companion array; value stack becomes registered — defense-in-depth even
     though the lexical-`this` env write protects it today),
  5. `js_eval_source_*` `Item` arrays (interim — absorbed by Merge C),
  6. eval binding journals' `Item` members (interim — absorbed by Merge C).
- **B3** — collapse the per-stack batch-reset calls into one registry walk;
  batch state-leak asserts (`js_assert_batch_runtime_state_clear`) check
  depth 0 per instance.

## B.4 Hazards

- `js_domain_stack` capture/replay (`js_domain_capture_stack` /
  `js_domain_apply_stack`) writes depths non-monotonically — the helper's
  restore must support setting an arbitrary depth with correct nulling in
  both directions (shrink nulls vacated slots; grow requires the caller to
  have written the slots first).
- Migration must not change observable semantics: `process.domain` syncing,
  with-binding cache invalidation, and super TDZ checks stay in client code.
- Registered ranges are scanned every collection; total added scan cost is
  a few hundred slots — negligible, but keep capacities as they are (no
  "generous" growth during migration).

## B.5 Acceptance gates

1. Baseline count holds after every per-stack migration commit.
2. Forced-GC repros pass: `temp/repro_with_stack_gc.js`,
   `temp/repro_super_this_gc.js`, `temp/repro_eval_local_gc.js` under
   `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
3. Clean removal: no `heap_register_gc_root_range` call for any migrated
   stack outside the helper; no bespoke reset function survives.
4. Zero MIR emission change (this merge is runtime-side only).

---

# Merge C: eval-frame consolidation (`JsEvalFrame`)

## C.1 Problem

An eval invocation currently brackets **seven stacks with independent
push/pop calls**: `js_eval_source_*` (filename, source, line offset, column
offset, compact flag — 5 parallel arrays, `js_runtime_state.cpp:10–15`) plus
frame-index stacks for the six binding journals (env, global-lexical, local,
lexical, immutable, private — `js_globals.cpp:17118+`). The lowering
(`js_mir_eval_lowering.cpp`) repeats `js_eval_source_pop()` across **~25
early-return branches** (lines 1442–1825), each of which must also emit the
right subset of frame pops. This is the same N-parallel-structures /
every-call-site-must-do-everything shape that produced the with-stack UAF,
and it already produced one incidental casualty: a silently swallowed
completion in nested eval (task_f7a9f03c).

## C.2 Target design

One frame record per eval invocation, one push, one pop:

```c
typedef struct JsEvalFrame {
    // Item members live in JsContextStack-managed arrays (Merge B):
    //   filename, source
    // POD members in a parallel struct array:
    int64_t line_offset, column_offset;
    bool    compact_stack;
    // watermarks into the six binding-journal entry arrays:
    int env_mark, global_lexical_mark, local_mark,
        lexical_mark, immutable_mark, private_mark;
} JsEvalFrame;

void js_eval_frame_push(Item filename, Item source, int64_t line_off,
                        int64_t col_off, bool compact);
void js_eval_frame_pop(void);   // restores all six journals + source info
```

- The variable-length binding **entry** arrays (`js_eval_env_bindings`,
  `js_eval_local_bindings`, …) are untouched — the frame stores watermarks
  into them, which is the established side-stack pattern.
- `Item` members (filename, source — and the journals' entry `Item`s from
  Merge B step 6) are rooted via the Merge B helper; POD members need no
  registration. This dissolves the split-array workaround that motivated the
  5-parallel-array layout.
- `js_eval_frame_pop` performs the journal restores currently done by
  `js_eval_env_pop_frame` / `js_eval_local_pop_frame` /
  `js_eval_private_pop_frame`, in the existing order.

## C.3 Phases

- **C1** — introduce `js_eval_frame_push/pop` as wrappers that call the
  existing seven push/pop entry points; no call-site changes yet. Verify
  bit-identical behavior.
- **C2** — rewrite `js_mir_eval_lowering.cpp` exits: replace every
  `js_eval_source_pop()` + frame-pop cluster with the single
  `js_eval_frame_pop()` (native-side scope guard or a common exit label —
  whichever matches the file's existing control flow). This is the
  high-value commit: ~25 duplicated cleanup sites → 1.
- **C3** — fold the seven stacks' storage into the frame record (Item arrays
  on the Merge B helper, POD struct array beside them); delete
  `js_eval_source_filename_stack` … `js_eval_source_compact_stack`, the six
  `*_frame_stack` arrays and their depth counters, and the now-unused
  individual push/pop entry points. Clean-removal rule applies: grep for
  every deleted symbol must come back empty.
- **C4** — close the audit gap: add a forced-GC regression for the journal
  `old_value` **restore** path (eval temporarily shadowing an existing
  binding, junk allocation, then frame pop → restored value must be live),
  which the 2026-07-24 probe could not reach.

## C.4 Hazards

- Exception paths: some eval exits run with a pending exception —
  `js_eval_frame_pop` must stay exception-neutral (no allocation, no
  exception check) exactly like the current pop sequence.
- Pop **order** between journals is behavior: local pop currently also pops
  lexical + immutable marks (`js_globals.cpp:17197–17209`); preserve the
  exact sequence inside `js_eval_frame_pop`.
- Nested evals: frames must nest LIFO; the task_f7a9f03c investigation
  (nested-eval silent abort) may land fixes in the same lines of
  `js_mir_eval_lowering.cpp` — rebase C2 on top of it, not the reverse.
- MIR emission: C2 changes which runtime symbols the eval lowering imports —
  expect MT7 budget diffs for eval-containing fixtures; lift deliberately.

## C.5 Acceptance gates

1. Baseline holds; eval-heavy JS suites and the Test262 eval sections in the
   baseline stay green.
2. Forced-GC: existing repros plus the new C4 old-value-restore regression
   pass under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
3. Clean removal: the seven retired stacks, their depth counters, and their
   individual push/pop entry points are gone (grep-verified); the ~25
   scattered `js_eval_source_pop()` call sites are reduced to the single
   frame pop.
4. MT7 budget changes (C2/C3 only) recorded in the commit message.

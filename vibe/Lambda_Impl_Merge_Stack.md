# Implementation Plan: JS Runtime Stack Merges

**Status:** implemented (2026-07-24). Merge A, Merge B, and the required
Merge C slices are complete. C3 was deliberately skipped: the env,
global-lexical, and private bridges retain different conditional lifetimes and
writeback ordering, so one bridge token would not reduce state safely.

**Design authority:** `Lambda_Design_Stack_API.md` §17 (decision record) and §8
(frame watermark discipline); Appendix A (stack inventory + audit findings).
Related: `Lambda_Design_Stack_Rooting.md` (safepoint-current canonical slots),
`Lambda_Design_MIR_Emission_Test.md` (MT7 emission ratchet, affected only by
phases that change MIR emission).

**Scope:** LambdaJS MIR-direct lowering and JS runtime only. The Lambda-side
transpiler never used `js_args_*`. C2MIR is frozen and out of scope.

This plan covers three independent merges, ordered by decision date:

- **Merge A — call-argument stack → side-root region.** `js_args_stack` is a
  hand-rolled duplicate of the side-root stack; its frames move onto the
  thread-local side-root region surfaced through the active `Context`. A
  *lifetime* merge.
- **Merge B — shared rooted-range mechanism.** The runtime hand-rolls
  epoch-guarded GC registration and batch clearing for several fixed global
  `Item` ranges. A base `JsRootRange` makes registration non-optional; an
  optional `JsItemStack` layer supplies ordinary depth/push/pop operations
  only to clients that actually have that shape. Semantic replacement,
  capture/replay, caches, and multi-field journals stay client-owned. A
  *mechanism* merge.
- **Merge C — unified eval state with lifetime-correct subframes.** One
  `JsEvalState` owns source contexts, per-call binding bridges, and
  caller-local journals, centralizing reset and diagnostics. Its `source`,
  `bridge`, and `local` substructures retain independent depths and lifetime
  APIs; they do not collapse into one push/pop frame. Narrow changes then
  remove duplication within each lifetime. A *state-ownership and
  lifetime-preserving data* merge.

Dependency order: A is independent. B1/B2's rooted-range and optional stack
layers land before C0 moves eval storage into `JsEvalState`; eval-owned range
migration then occurs in C rather than B3. B's non-eval client migrations may
continue independently. The standalone `js_with_stack` rooting fix
(task_b9e61cf7) is already landed and is B's canary and migration baseline.

## Completed implementation record (2026-07-24)

- **Merge A:** `lambda_side_root_alloc_n()` now reserves and zeroes exact
  side-root slots before publishing the watermark. `js_args_*` uses that
  region, checks allocation failure before its first generated store, and the
  private buffer plus both lifecycle hooks are gone.
- **Merge B:** `JsRootRange` owns per-epoch exact-range registration and the
  batch reset registry; `JsItemStack` owns only ordinary LIFO slots. The with,
  domain, CommonJS-module, and super-this stacks use it; cache and table roots
  that have different semantics remain separate.
- **Merge C:** `JsEvalState { source, bridge, local }` owns all eval storage.
  Each `Item` column is registered precisely, the source wrapper has exactly
  one owning pop, and local frame marks are one record stack with an explicit
  failure branch. Source, bridge, and local depths remain distinct.
- **Conservative JIT metadata correction:** `js_clear_exception`, debug
  exception assertions, and `lambda_async_frame_set_word` are MAY_GC because
  their error/re-materialization paths can allocate or log. Their new
  safepoints and root stores are recorded in `test/mir/mir_budgets.json`.
- **Verification:** `make test-lambda-baseline` passed **3,607 / 3,607**;
  `make test-gc-rooting-core` passed with all forced-GC gates and the static
  effect/root-hazard audits; `test_mir_ratchet_gtest.exe` passed **15 / 15**.

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
Phase 3 deletes them). For Merge B it means each migrated range's bespoke
registration/reset boilerplate goes in the same commit as its migration. For
Merge C it means only the duplicated structures explicitly replaced by a
completed slice; source-context and bridge/local-journal APIs with different
lifetimes must remain separate. A merge that leaves replaced boilerplate
behind, or deletes a still-distinct lifetime boundary, is incomplete
regardless of test results.

---

## 1. Current state

### 1.1 Replaced private argument stack

The former 256 K-slot, process-global `js_args_stack` was removed completely:
its full-capacity root registration, `js_alloc_env` overflow fallback,
pop-time clearing invariant, batch reset, process cleanup, declarations, and
all call sites are absent. The surviving ABI entry points in
`lambda/js/js_runtime_function.cpp` now use the active `Context`:

- `js_args_push(count)` reserves zeroed slots via `lambda_side_root_alloc_n`.
  A NULL result is surfaced to generated MIR before any argument store.
- `js_args_save()` returns the current `side_root_top` watermark as an
  integer-sized token without allocating.
- `js_args_restore(mark)` validates integer bounds/alignment and only rewinds
  the root watermark; the collector never scans words above that top.

This changes the collector from scanning a permanent private capacity to
scanning only the live side-root prefix.

### 1.2 Current JIT watermark protocol

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

One new runtime primitive plus a reimplementation of the three surviving
`js_args_*` entry points on top of it. The JIT-facing names and C-call ABI are
preserved through Phase 3. Phase 2 adds one required NULL check after
`js_args_push`; Phase 4 may optionally inline save/restore after measurement.

```c
// side_stack.h — root-side twin of lambda_side_number_alloc().
// Reserves and ZEROES n contiguous slots above side_root_top, publishes the
// new top, returns the old top. NULL on reserve/ensure failure.
uint64_t* lambda_side_root_alloc_n(Context* context, size_t slot_count);
```

Semantics changes:

| Aspect | Before | After |
|---|---|---|
| Storage | private 2 MB calloc | side-root reservation slots |
| GC visibility | full-capacity root-range registration | scanned as part of `[base, top)`; no registration |
| Zeroing | pop-time memset (maintains `[len, cap)` invariant) | push-time zeroing before top publish (same rule as `lambda_root_frame_begin`) |
| Mark | stack length (`int64_t`) | saved `side_root_top` pointer (as `int64_t`) |
| Overflow | unowned `js_alloc_env` fallback | set stack-overflow error, return NULL, and branch before the first argument store |
| Batch reset | explicit memset + drop registration | enclosing generated epilogues or audited recovery checkpoints restore the watermark |
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
- `make test-gc-rooting-core` green.
- Confirm the MIR emission fixtures are green so Phases 2 and 4 have an
  attributable baseline (`test/mir/mir_budgets.json`, MT7 ratchet).
- Audit every `js_batch_reset()` / `js_batch_reset_to()` entry point and record
  the generated epilogue, recovery checkpoint, or explicit snapshot that owns
  its side-stack rewind. This audit must finish before Phase 2 turns the old
  private reset into a no-op.

### Phase 1 — runtime primitive

**Status: implemented (2026-07-24).** `lambda_side_root_alloc_n` now owns the
shared zero-before-publish reservation logic used by both direct callers and
`LambdaRootFrame`. `GCHeapTest.SideRootAllocationZerosAndKeepsItemsAlive`
proves exact-range liveness while a native frame is nested above the range,
then proves collection after restoring the saved watermark. Focused build and
the full GC heap binary passed (61 / 61); the Phase 0 full-repository baseline
is still required before Phase 2 changes JS argument lowering.

`lambda/runtime/side_stack.c` / `side_stack.h`:

1. Add `lambda_side_root_alloc_n(Context*, size_t)`:
   - bind-on-demand like `lambda_side_number_alloc` (call
     `lambda_side_stack_ensure(ctx, n, 0)`);
   - zero the `n` slots, **then** advance `side_root_top` — comment must state
     the ordering reason (collection during argument evaluation must never
     scan stale words as roots);
   - return the pre-bump top; NULL on failure.
2. No callers yet; unit-test via a small GTest in the existing side-stack /
   rooting test file (push N, force `gc_collect_with_root_region` over the
   exact active `[side_root_base, side_root_top)` range that
   `heap_gc_collect` delegates to, verify that slots are scanned; verify LIFO
   restore interop with
   `lambda_root_frame_begin/end` frames above it).

Gate: `make build-test` + new unit tests green.

### Phase 2 — reimplement `js_args_*` and make allocation failure explicit

**Status: implemented.** The generated failure branch and its intentional MIR
delta are present; the checked-in side-stack regression exercises the path
under forced collection.

`lambda/js/js_runtime_function.cpp`:

1. Delete `JS_ARGS_STACK_CAP`, `js_args_stack`, `js_args_len`,
   `js_args_registered`.
2. `js_args_push(count)`:
   ```
   if (count <= 0) return NULL
   p = lambda_side_root_alloc_n(context, count)
   if (!p) {
       lambda_stack_overflow_error("js-args-side-stack")
       return NULL
   }
   return (Item*)p;
   ```
3. `js_args_save()` → `return (int64_t)(uintptr_t)context->side_root_top;`
   (keep `AutoAssertNoGC`).
4. `js_args_restore(mark)`:
   - compare `uintptr_t` values, not relationally compare possibly unrelated
     C pointers; require an aligned mark in `[side_root_base, side_root_top)`;
   - no-op for an invalid/stale mark, including one already rewound past by an
     enclosing exceptional restore;
   - `context->side_root_top = (uint64_t*)mark;` — **no memset** (GC never
     reads above top). Under `!NDEBUG`, optionally poison popped words with
     a non-pointer pattern to catch retained aliases deterministically
     (match the `0xA5` convention of `lambda_restore_number_frame_top`).
5. In `js_mir_function_collection_class_inference.cpp`, test the
   `js_args_push` result before the first store. NULL branches to the existing
   function overflow/error return path; it must never fall through to an
   argument-slot write.
6. After Phase 0's reset audit is recorded, make
   `js_args_stack_reset()` / `js_args_stack_cleanup()` transitional no-ops for
   this phase (removed in Phase 3). If the audit found an uncovered boundary,
   land its explicit snapshot restore before these bodies become empty.
7. Update the block comment at the top: the "base never moves / cannot
   realloc" rationale now cites the reservation model.

Gate: full `make test-lambda-baseline` (must hold the Phase 0 count) plus
checked-in forced-GC regressions for deep nested calls, throws during argument
evaluation, and injected side-root allocation failure. The exception-edge
restore in `js_mir_completion.cpp` is the risky path. Review and record the
small intentional MIR delta from the new NULL branch.

### Phase 3 — delete the private lifecycle

**Status: implemented.** The reset/cleanup hooks, declarations, and private
storage have been deleted after the recovery/watermark audit.

1. Remove the `js_args_stack_reset()` call from
   `js_reset_transient_call_state()` (`js_runtime_state.cpp:1232`) and the
   `js_args_stack_cleanup()` call from `lambda/main.cpp:440`; delete both
   functions and their declarations (`js_runtime.h:235–236`).
   - Audit every `js_batch_reset()` / `js_batch_reset_to()` caller before
     deletion. Do not assume they all call `lambda_side_stack_reset`: the main
     batch loop and several hosted/runtime paths use
     `LambdaRecoveryCheckpoint`, while ordinary generated returns use their
     epilogues. Record the owning watermark restore for each reset entry point.
     If any boundary can be reached with a non-base `side_root_top` and no
     enclosing checkpoint, add an explicit snapshot restore there.
2. Grep for any remaining `heap_register_gc_root_range` special-casing tied
   to the old buffer — none expected beyond the deleted site.

Gate: baseline green; run the batch/multi-test harness specifically (heap
teardown + recreation across tests is what `js_args_stack_reset` existed
for).

### Phase 4 (optional, measured and deferred by default) — inline save/restore

**Status: skipped — not measured.** The C ABI wrappers remain because this
implementation is a lifetime/rooting change, not an unverified call-path
micro-optimization.

Marks are now just `side_root_top` values, so the two C calls per
argument-carrying call expression can become direct `Context` field ops using
the frame's runtime register (`mt->em.frame.runtime`, same pattern as
`em_store_frame_top` with `offsetof(Context, side_root_top)`):

- save (`js_mir_function_collection_class_inference.cpp:3162`) → MIR load of
  `context->side_root_top` into the mark reg;
- restore (`js_mir_expression_lowering.cpp:13267`,
  `js_mir_completion.cpp:250` and `:271`) → load the current top and emit the
  same release-build validity/order check as `js_args_restore` before storing
  the mark. A plain unconditional store is forbidden: an older exceptional
  restore may already have rewound below a nested mark, and storing that mark
  would move the top upward and resurrect popped roots.
- `js_args_push` stays a C call (it needs ensure/commit growth + zeroing).

Costs to plan for:

- **MT7 emission budgets**: instruction/call counts change in
  `test/mir/mir_budgets.json` fixtures — manual budget lifts required, with
  the delta recorded in the commit message.
- Build with `make release` and bench before/after on call-heavy Result-suite
  scripts to confirm the win justifies the budget churn; if not measurable,
  skip this phase entirely.

### Phase 5 — clean removal audit and records

**Status: implemented.** The deletion grep is clean for every retired
argument-stack symbol; the implementation record at the start of this file
contains the final test and MIR-budget evidence.

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
  (`jm_oldest_arg_stack_mark`) — the restore protocol is unchanged through
  Phase 3.

### 4.2 Hazards

- **Restore-guard semantics.** The old guard (`mark >= len` → no-op) silently
  tolerated a stale mark. The new integer-address guard must tolerate an
  enclosing restore that already rewound below the mark; storing such a mark
  would move the top *up* and resurrect roots. Validate base, top, and slot
  alignment before restoring. The optional Phase 4 must preserve this check in
  release MIR.
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
- **Threading.** The old argument buffer is process-global; the side regions
  and active `context` are thread-local. Moving arguments changes their
  storage isolation even though most semantic JS runtime state remains global.
  Record that narrower improvement, and the remaining global-state boundary,
  in the JT globals ledger (`Lambda_Js_Thread.md` §6.5).
- **Parallel baseline flakiness.** `test-lambda-baseline` is known to flake
  1–2 heavy tests under parallel load; re-verify any failure standalone
  before attributing it to this change.

### 4.3 Overflow decision

Remove the `js_alloc_env` fallback in Phase 2. `heap_calloc_closure_env`
describes how to trace an environment after the environment is marked; it
does not make an otherwise ownerless raw pointer a precise GC root. During
argument evaluation, later arguments may allocate and collect before any
closure/function owns the fallback. Because conservative native-stack
scanning is retired, preserving that path would preserve a latent lifetime
bug.

Allocation failure therefore calls `lambda_stack_overflow_error`, returns
NULL, and takes an emitter-side failure branch before any slot write. The
failure-injection regression must prove that the path reports the error
without a NULL write or a leaked watermark.

---

## 5. Acceptance gates

1. Phases 1–3: `make test-lambda-baseline` at the Phase 0 count and
   `make test-gc-rooting-core` green. Checked-in call-heavy,
   throw-in-arglist, and allocation-failure regressions run under
   `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. Phase 2's required
   NULL-branch MIR delta is reviewed and recorded; unrelated emission is
   unchanged.
2. Phase 3 additionally: multi-test batch harness green (heap
   teardown/recreation across tests).
3. Phase 4 (if taken): `make release` used, budgets lifted deliberately, and
   bench delta recorded; otherwise explicitly record "Phase 4 skipped — not
   measurable".
4. **Clean removal verified**: the Phase 5 deletion checklist is empty — the
   buffer, cap macro, length/registration statics, root-range registration,
   pop-time memset, both lifecycle functions, their declarations, their call
   sites, and all old-model comments are gone. The grep commands in Phase 5
   return no hits under `lambda/`; no transitional stub, dead branch, or
   `#if 0` block remains. A merge that leaves any of these behind is
   incomplete regardless of test results.

---

# Merge B: shared rooted-range mechanism (`JsRootRange` + `JsItemStack`)

## B.1 Problem

Several semantic globals repeat epoch-guarded GC registration and batch
clearing, but they do **not** all share stack semantics (Appendix A of
`Lambda_Design_Stack_API.md`):

| Storage | Location | Current rooting / shape |
|---|---|---|
| `js_domain_stack` (64) | `js_runtime.cpp:~30480` | rooted; whole-stack capture/replay and replacement |
| `js_cjs_module_stack` | `js_mir_entrypoints_require.cpp:~1628` | rooted; top pop plus removal from the middle |
| `js_eval_source_*` | `js_runtime_state.cpp:10–23` | two rooted `Item` arrays sharing one depth with POD companions |
| `js_with_stack` + cache | `js_globals.cpp:~16288` | **fixed 2026-07-24**: epoch-rooted, vacated slots nulled, regression checked in |
| `super_this_*` | `js_runtime_state.hpp:65–67` | `Item` values not separately rooted; `bool` companion |
| eval binding journals | `js_globals.cpp:~17167` | multi-field entry arrays plus independent watermark stacks; no dedicated roots |

The shared bug-prone mechanism is ownership of a fixed `Item` root **range**:
register it before the first live store, re-register it for each heap epoch,
clear stale Items at reset, and null vacated entries. Push/pop/depth is only an
optional specialization. Treating caches, replayable domain state, and
multi-field journals as one universal stack would move semantic operations
into the wrong layer.

## B.2 Target design

Use two layers:

```c
// js_runtime_state.hpp — client-owned storage remains at a stable address.
typedef struct JsRootRange {
    Item* slots;
    int slot_count;
    uint64_t roots_epoch;
    const char* name;
} JsRootRange;

bool js_root_range_ensure_registered(JsRootRange* range);
void js_root_range_clear(JsRootRange* range);

typedef struct JsItemStack {
    JsRootRange roots;
    int depth;
} JsItemStack;

bool js_item_stack_push(JsItemStack* stack, Item value);
Item js_item_stack_top(const JsItemStack* stack);
void js_item_stack_pop(JsItemStack* stack);       // nulls the vacated slot
void js_item_stack_clear(JsItemStack* stack);     // nulls [0, depth)
void js_item_stack_shrink(JsItemStack* stack, int depth); // never grows
```

Rules:

- **Registration is non-optional.** Every write API calls
  `js_root_range_ensure_registered()` before storing a live `Item` and fails
  without writing if no active heap can accept the range. Direct client writes
  are allowed only after a successful explicit ensure in the same operation;
  grep and tests enforce this boundary.
- `JsItemStack` only serves true single-`Item` stacks. Whole-stack
  replacement uses clear + push; middle removal shifts client-visible slots
  then calls `shrink` to null the tail. `shrink` never grows into unwritten
  slots.
- Fixed cache roots use `JsRootRange` directly. Eval-owned Item arrays use
  ranges embedded in `JsEvalState` substructures and retain their
  substructure-owned semantic depths. Non-`Item` companion arrays are never
  registered.
- Multi-field eval journals do not register their interleaved structs as an
  `Item[]`. Before migration, choose an exact layout: structure-of-arrays or
  flattened contiguous `Item` fields, with POD fields separate. Each precise
  Item range is registered through `JsRootRange`.
- A fixed-capacity registry descriptor stores stable static range/owner
  pointers plus a client reset callback. Registration into this descriptor
  registry happens inside the helper, once per static instance, without C++
  static constructors. Registry overflow makes the ensure operation fail
  before any Item write. The batch reset walks the registry; custom callbacks
  preserve semantic cache invalidation and companion-depth resets.

## B.3 Phases

- **B0 — implemented prerequisite:** task_b9e61cf7 is landed. Keep
  `test/js/regression_with_stack_gc.js` green and treat the existing
  `js_with_register_roots` behavior as the migration oracle.
- **B1 — implemented root-range base:** `JsRootRange` and the reset descriptor
  registry. Unit-test registration before first store, epoch
  re-registration, range clearing, duplicate descriptor suppression,
  registry overflow failure, and forced-GC visibility.
- **B2 — implemented optional stack layer:** `JsItemStack` provides checked
  push, pop/shrink nulling, clear, and the prohibition on growing by `shrink`.
  The fixed-capacity helper itself is exercised by the migrated forced-GC
  clients; capacity failures return false before publishing a value.
- **B3 — implemented client migration:**
  1. `js_with_stack` → `JsItemStack`; its two last-binding cache Items remain a
     separate two-slot `JsRootRange`. Preserve cache invalidation exactly.
  2. `js_domain_stack` → `JsItemStack`; replay becomes clear + filtered pushes,
     while `process.domain` synchronization stays in domain code.
  3. `js_cjs_module_stack` → `JsItemStack`; middle removal still shifts slots
     in CJS code and then shrinks. Do not disturb the separate
     `js_cjs_module_names` / `js_cjs_module_objects` table roots.
  4. `super_this_value_stack` → `JsItemStack` with the existing parallel
     `bool` array retained and reset by the client callback.

  Eval-owned source and journal ranges are intentionally not B3 clients:
  C0 moves them together into `JsEvalState`, using the already-landed B1/B2
  mechanisms without splitting ownership across two migrations.
- **B4 — implemented reset consolidation:** migrated ranges register in the
  central batch-reset registry; stack callbacks clear semantic depths while
  clients retain their non-mechanical cache/replay invalidation.
  Remove each migrated client's bespoke
  registration and mechanical clearing. Keep semantic reset functions when
  they do more than clear roots/depth. Extend
  `js_assert_batch_runtime_state_clear` to verify every semantic depth is zero.

## B.4 Hazards

- These ranges are process-global semantic state, not per-`Context` data;
  `JsRootRange` intentionally avoids the misleading `ContextStack` name.
- `js_heap_epoch` is the canonical re-registration discriminator. Do not mix
  epoch and raw-GC-pointer ownership schemes inside the helper.
- Domain replay and with-stack replacement must populate by clear + checked
  pushes. CJS middle removal must null the final vacated slot.
- Eval journal structs contain multiple Items and POD fields. Registering the
  raw struct bytes as an Item range would create false roots and is forbidden.
- Registry reset order must not dereference Items from a destroyed heap. The
  registry is walked while storage addresses are valid and only clears words /
  client POD state.
- Registered scan cost remains bounded by the current capacities; do not grow
  capacities as part of this refactor.

## B.5 Acceptance gates

1. `make test-lambda-baseline` holds the baseline count after every client
   migration; Merge B itself emits no MIR changes.
2. `make test-gc-rooting-core` runs checked-in with, super, eval-read, and
   eval-restore regressions under
   `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. Promote useful
   `temp/repro_*.js` probes into `test/js/` with matching expected output
   before counting them as gates.
3. Clean removal is per migrated **range**: its bespoke
   `heap_register_gc_root_range` / epoch / mechanical-clear path is gone.
   Client semantic reset, replay, cache invalidation, and writeback logic
   remain where required.
4. Batch and heap-recreation harnesses prove descriptor registration is
   duplicate-free and re-registers every range for the new heap.

---

# Merge C: `JsEvalState` with lifetime-correct subframes

## C.1 Current lifetime model

The eval-related structures belong to one runtime subsystem and should share
one state owner, but they do not bracket one common activation:

1. **Source context** — `js_eval_source_*` stores filename/source/offsets for
   stack synthesis. `js_builtin_eval_with_options()` owns the successful
   push/pop pair around its inner evaluator. The same source stack also
   brackets ordinary function
   invocation for VM-provided compact stack metadata through
   `js_eval_source_push_compact()` in `js_runtime.cpp`; it is not eval-only.
2. **Per-call bridges** — env and global-lexical frames are created in
   generated direct-eval caller code before `js_builtin_eval`; private frames
   are independently conditional on a current class with private names.
   Their pops occur after the runtime call, with env writeback before env pop.
3. **Caller-local journal** — local, lexical, and immutable frames are lazily
   opened on the first direct eval in a generated function and remain active
   until that generated function exits. They preserve variables introduced by
   one direct eval for later direct evals in the same caller.

Therefore one `JsEvalState` can own and reset all eval data, but there is no
valid single `JsEvalFrame` that combines all source and journal watermarks.
The frame/consolidation boundary remains each lifetime above.

## C.2 Target design

### C.2.1 Common state owner

Move the scattered eval globals into one state capsule embedded in
`JsRuntimeState`:

```c
struct JsEvalState {
    JsEvalSourceState source;  // runtime-eval / VM-source-call lifetime
    JsEvalBridgeState bridge;  // one generated direct-eval call
    JsEvalLocalState local;    // generated caller-function lifetime
};

struct JsRuntimeState {
    // ... existing runtime fields ...
    JsEvalState eval = {};
};
```

`JsEvalSourceState`, `JsEvalBridgeState`, and `JsEvalLocalState` own their
current storage, capacity, depth/watermarks, and exact `JsRootRange`
descriptors. MIR-facing C ABI wrappers continue to use the active
`js_runtime_state.eval`; internal helpers take a `JsEvalState*` or the
appropriate substate pointer explicitly.

The common owner provides:

```c
void js_eval_state_reset(JsEvalState* state);
void js_eval_state_assert_clear(const JsEvalState* state,
                                const char* reset_name);
```

Rules:

- `JsEvalState` centralizes storage ownership, batch reset, capacity
  diagnostics, and leak assertions; it does not have a shared depth.
- There is no `js_eval_state_push()` / `js_eval_state_pop()`. Callers operate
  on `source`, `bridge`, or `local` according to the lifetime they own.
- Never register the raw `JsEvalState` bytes as an Item range: it contains
  Items, integers, booleans, and padding. Each contiguous Item field group is
  registered through its own `JsRootRange`.
- Move one substate at a time and delete its old file-static arrays/counters in
  the same commit. No compatibility mirror or duplicate authoritative depth
  survives.

### C.2.2 Source-context cleanup

Split `js_builtin_eval_with_options()` into a thin ownership wrapper and an
inner evaluator:

```c
static Item js_builtin_eval_execute(/* validated eval inputs */);

extern "C" Item js_builtin_eval_with_options(/* existing ABI */) {
    // preserve the existing non-string/empty-source fast paths
    // before a source context is required
    bool source_pushed = has_eval_source && js_eval_source_push(...);
    Item result = js_builtin_eval_execute(...);
    if (source_pushed) js_eval_source_pop();
    return result;
}
```

All current returns after source-frame acquisition move into
`js_builtin_eval_execute()`, which does not own the source frame and never
pops it. This yields one normal-return pop without coupling source state to
binding journals. Do not use a C++ destructor as the only cleanup: non-local
recovery bypasses destructors. Batch/reset handling for a skipped pop remains
explicit through Merge B's source-range reset callback.

Make both source push functions return whether they actually appended a
frame. Capacity failure must not be followed by a pop, which would otherwise
remove an enclosing source context. `js_eval_source_push_compact()` and the
two ordinary function-call sites adopt the same success token; they share the
source-context mechanism but not eval bridge lifetime.

### C.2.3 Caller-local watermark record

Replace the three parallel local/lexical/immutable frame-index stacks with one
POD record stack:

```c
typedef struct JsEvalLocalFrameMarks {
    int local_mark;
    int lexical_mark;
    int immutable_mark;
} JsEvalLocalFrameMarks;
```

`js_eval_local_push_frame()` appends one record and returns success;
`js_eval_local_pop_frame()` restores the three counts in their current order.
The emitter sets `eval_local_frame_reg` only after a successful push and routes
capacity failure through an explicit error path; a failed push must never
cause a later pop of an enclosing frame. The variable-length binding entry
arrays and the function-lifetime protocol are otherwise unchanged. This
removes three synchronized depth counters without changing the activation
lifetime.

### C.2.4 Optional per-call bridge token

After C.2.1 through C.2.3 are stable, audit the direct-eval call sequence in
`js_mir_expression_lowering.cpp`. If env/global-lexical/private brackets can
be represented without losing their conditionality or env writeback order,
introduce a separate `JsEvalBridgeFrame` with active flags and the relevant
entry-array watermarks:

```c
typedef struct JsEvalBridgeFrame {
    int env_mark;
    int global_lexical_mark;
    int private_mark;
    bool env_active;
    bool global_lexical_active;
    bool private_active;
} JsEvalBridgeFrame;
```

The emitter begins only the required bridges, populates bindings through the
existing APIs, calls eval, performs env writeback, then ends the bridge once.
This frame never contains source context or caller-local
local/lexical/immutable marks. If the audit cannot preserve the exact
conditional order with fewer operations, skip this optional slice.

## C.3 Phases

- **C0 — implemented `JsEvalState` and root correctness:** after
  B1/B2 land, promote the eval-local read probe and an `old_value` restore
  probe into checked-in forced-GC regressions. Add
  `JsEvalSourceState` / `JsEvalBridgeState` / `JsEvalLocalState`, embed their
  common `JsEvalState` owner in `JsRuntimeState`, and move one substate's
  storage per commit. Convert interleaved journal Item fields to precisely
  registerable ranges while keeping POD fields separate. Route batch reset
  and clear-state assertions through `js_eval_state_reset` /
  `js_eval_state_assert_clear`. Preserve every existing lifetime/API during
  this mechanical ownership move.
- **C1 — implemented source wrapper:** `js_builtin_eval_execute()` has no
  source-pop path; its ABI wrapper owns the one successful push/pop pair.
  Both source pushes return success tokens. The forced-GC eval gates cover
  ordinary and nested/local evaluation.
  Extract `js_builtin_eval_execute()` and leave one
  source pop in the ownership wrapper. Change both source pushes to return a
  success token and pop only on success. Verify behavior and source/stack
  diagnostics are unchanged for success, parse/early errors, nested eval,
  capacity failure, and thrown eval.
- **C2 — implemented local marks:** `JsEvalLocalFrameMarks` replaced the
  three frame-index arrays and redundant depths; the emitter branches on a
  failed push before it marks a local frame active. Delete
  `js_eval_local_frame_stack`, `js_eval_lexical_frame_stack`,
  `js_eval_immutable_frame_stack`, and the two redundant lexical/immutable
  depths. Make `js_eval_local_push_frame` return success and make the emitter
  branch on failure before marking the function-lifetime frame active. Keep
  push/pop as the MIR-facing lifetime boundary.
- **C3 — skipped after call-sequence audit:** env/global/private bridges have
  distinct conditionality and env writeback order, so a common token would
  not remove code safely. Their shared `JsEvalBridgeState` ownership and
  exact root ranges are implemented without flattening their APIs.
  Consolidate only env/global/private
  per-call brackets after a call-sequence audit. Preserve env writeback before
  restore and preserve pending exceptions. Record any MIR import/count change.
- **C4 — implemented removal audit:** the retired file-static eval storage and
  old local frame arrays are gone. Grep only for symbols retired by C0–C3. Do not
  require `js_eval_source_*`, env/global/private entry arrays, or the
  caller-local push/pop API to disappear; they still represent distinct
  semantics. Do require the superseded file-static storage/counters to be gone
  after their authoritative fields move into `JsEvalState`.

## C.4 Hazards

- Source cleanup and bridge cleanup occur on opposite sides of
  `js_builtin_eval`. Combining them would move ownership across the runtime
  ABI and is forbidden.
- A common `JsEvalState` is an ownership/reset capsule, not proof of a common
  activation. Do not introduce a shared depth or whole-state push/pop merely
  because the substates are adjacent in memory.
- `js_eval_source_push_compact()` serves ordinary VM-source function calls.
  Source storage and APIs must remain general source-context infrastructure.
- Env writeback must occur before env frame restoration. Global-lexical and
  private frames are conditional and must not acquire empty synthetic frames
  merely to fit an abstraction.
- The caller-local journal persists across multiple eval calls in one
  function. Popping it per eval would break `eval("var x = 1"); eval("x")`.
- Pop/restore with a pending exception must remain exception-neutral and must
  not allocate. Preserve the current local → lexical → immutable restoration
  order.
- Every conditional push needs an explicit success token. Capacity failure
  followed by an unconditional pop can corrupt an enclosing nested frame.
- Nested-eval work from task_f7a9f03c overlaps the source wrapper lines; rebase
  C1 on the live fix.
- C1 changes native runtime control flow only. C2's required push-result branch
  and C3's optional runtime-symbol consolidation require MT7 review.

## C.5 Acceptance gates

1. `make test-lambda-baseline` holds; eval-heavy JS and baseline Test262 eval
   sections remain green after each phase.
2. `make test-gc-rooting-core` includes checked-in eval-local read and
   `old_value` restore regressions under
   `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
3. C1 specifically covers every former early-return class plus nested eval,
   thrown eval, and ordinary `js_eval_source_push_compact` call-stack
   behavior.
4. Source-depth and local-frame capacity-failure tests prove that a failed
   push never pops an enclosing frame.
5. C0 leaves one authoritative `JsEvalState` owner: old file-static eval
   storage is gone, `js_eval_state_reset` clears all three substates, and
   `js_eval_state_assert_clear` reports each substate depth independently.
   Exact root registration covers only Item ranges, never the whole struct.
6. C2's three old frame-index arrays and redundant depths are gone; all
   distinct source, bridge, and caller-local lifetime APIs remain. Its
   push-result MIR delta is recorded.
7. C3, if taken, records MT7 changes and proves env writeback, global lexical,
   private-name, pending-exception, and nested-eval behavior. Otherwise record
   "C3 skipped — conditional bridge shape did not justify unification."

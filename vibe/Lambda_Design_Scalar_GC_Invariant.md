# Lambda Design: Scalar GC-Heap Elimination (No-Scalar-Cell Invariant)

**Status: PROPOSED** — rev 3, 2026-07-22. **Direction confirmed by user
2026-07-22**: keep the no-scalar-cell invariant and caller-donated-home model,
with the transitive lifetime, context-boundary, release-enforcement, layout,
gate, and documentation corrections incorporated below. The C2MIR freeze is
recorded as CLAUDE.md/AGENTS.md rule 14.

## 1. Goal

Establish and enforce the invariant:

> **No `INT64`, `UINT64`, or `FLOAT`/`FLOAT64` payload is ever allocated in the GC
> object zone.** Every wide-scalar payload lives in exactly one of:
> (a) inline in the `Item` word (normal doubles, ±0),
> (b) an activation/caller-owned one-word scalar home (a side-number-stack
>     slot or an explicit donated C home),
> (c) a destination-owned payload word inside a container / env / struct
>     (`array_set` tails, `owned_item_slot_store` slots, map fields),
> (d) static AST / input-arena storage.

Corollary for the collector: an `Item` tagged `INT64`/`UINT64`/`FLOAT` can
**never** point into the GC zone, so `item_to_ptr()` may return NULL for those
tags without a zone lookup.

Because the collector will rely on this for correctness, this is an
**all-build invariant**, not a debug-only assumption. Release allocation paths
must reject a scalar GC type tag before SG7 is enabled.

This is a design invariant first and a GC optimization second. What it buys:

- **One ownership doctrine, no exceptions.** Today the sized-int end-state
  table (`Lambda_Impl_Sized_Int (done).md` §0) has one asterisk: "Item-only
  public/opaque persistence → GC scalar cell." Removing it makes the rule
  quotable in one sentence and removes the "maybe GC-owned" case from every
  audit of scalar lifetime.
- **A class of lifetime bugs becomes unrepresentable.** No code path can ever
  again depend on a scalar cell being traced.
- **Collector simplifications**: marker early-out on three tags, smaller
  GC-audit/verifier arms, and deletion of `scalar_heap.cpp` + its counters.
- **Less churn**: the builtin double sites currently allocate a GC cell per
  call that is instant garbage for ~all real values (inline encoding wins);
  eliminating them cuts allocation-rate-driven collections and sweep length
  (sweep walks every header, alive or dead — `gc_heap.c` `gc_sweep`).

Out of scope: `DTIME` (GC-owned `DateTime` by design), `DECIMAL`/BigInt,
strings/symbols/binary. They are reference types with real identities; the
invariant is about the three number types that already have non-heap homes.

## 2. Verified residue inventory (2026-07-22)

### P1 — `lambda_item_heap_rehome()` (`lambda/scalar_heap.cpp`)

The only INT64/UINT64 GC producer in the tree, and one FLOAT arm. Call-site
families:

| # | Site | Why it heap-rehomes today |
|---|------|---------------------------|
| P1.1 | Lambda public wrapper epilogue — `transpile-mir.cpp:12214` | boxed-ABI caller donated no home; wrapper restores its watermark on exit |
| P1.2 | JS frame epilogue without incoming home — `js_mir_hashmap_scope_utils.cpp:431` | same (public entries only; internal calls take the `incoming_scalar_home` branch) |
| P1.3 | JS public wrapper lowering — `js_mir_function_class_lowering.cpp:636` | same |
| P1.4 | Python public wrapper — `py/transpile_py_mir.cpp:611` | same |
| P1.5 | `js_throw_value` — `js_runtime_state.cpp:379` | exception slot outlives the throwing frame |
| P1.6 | `bind()` `bound_this` — `js_runtime.cpp:13555, 13653` | bound function object outlives all frames |
| P1.7 | Jube host-data API — `jube_registry.cpp:1128` (`jube_host_data_item_heap_rehome`) | convenience "make safe to hold" entry exposed to native modules |

### P2 — direct `heap_alloc/heap_calloc(sizeof(double), LMD_TYPE_FLOAT)`

44 builtin sites, all following the same 3-line pattern
(`alloc; *fp = v; lambda_float_ptr_to_item(fp)`):

| File | Sites |
|------|-------|
| `js/js_globals.cpp` (Date ms et al.) | 30 |
| `js/js_os.cpp` | 3 |
| `js/js_fs.cpp` (stat times) | 2 |
| `js/js_mir_entrypoints_require.cpp` | 2 |
| `js/js_canvas.cpp` (TextMetrics) | 1 |
| `py/py_builtins.cpp` | 4 |
| `py/py_bigint.cpp` | 1 |
| `module/radiant/radiant_module.cpp` | 1 (`heap_calloc`) |

Since `lambda_float_ptr_to_item` returns the inline encoding for every normal
double, these cells are almost always instant garbage; the pointer is kept only
for tiny/subnormal values.

The 44 sites divide into two lifetime classes and must not be migrated as one
mechanical batch:

- **P2A — 42 ordinary builtin sites.** Most return to a live generated frame
  or immediately enter a destination-owned property/array/tuple slot. Each
  must still be classified by its immediate consumer before conversion to
  `push_d`; direct returns are safe only when the transitive SG2 return-home
  contract is already satisfied by their caller chain.
- **P2B — 2 isolated-JS context-boundary sites**
  (`js_mir_entrypoints_require.cpp:327,1029`). They deliberately construct
  `final_result` before restoring `context = old_context`. A number-stack
  `push_d` in the departing context would not survive that boundary. These
  sites migrate with SG2 by donating a home from the caller of the top-level
  `transpile_js_*`/execution entry API; they are not part of the ordinary
  builtin sweep.

### P3 — non-GC mutable scratch homes

`js_globals.cpp` also contains `date_buf[16]` and `gt_buf[16]` scratch rings
used with `lambda_float_ptr_to_item`. TimeClip currently makes their outputs
normally inline, but mutable ring storage is outside the ownership taxonomy
above and would be clobber-prone if a pointer-backed value ever reached it.
Remove both during SG6: the property path uses `push_d` followed immediately by
the destination-owned property store; the return path follows SG2.

### Already compliant (for the record)

- Transients: `box_int64_value` / `box_uint64_value` / `box_float_number_stack`
  → side number stack.
- Internal calls: caller-donated homes (`lambda_item_adopt_scalar_home`).
- Containers/envs/module slots/mailboxes/task results: destination-owned
  payloads (`array_set` tails, `owned_item_slot_store`), reads rematerialize
  via `scalar_storage_read`.
- C2MIR path: `transpile.cpp` emits no watermark save/restore, so its
  number-stack values live until frame-scan reclamation — no dangling, no GC
  cells, no rehome calls.
- `lib/`, `lambda/input/`, `lambda/format/`: zero GC scalar allocation sites.
  `lambda/module/radiant/radiant_module.cpp` has the one P2A helper listed
  above.

## 3. Decisions

### SG1 — The invariant, enforced at the allocation choke point

State the invariant in `Lambda_Design_Stack_API.md` (amend §15) and here.
Enforcement is mechanical at the shared GC allocation layer:

- `gc_heap_alloc` and the pre-classified bump/class allocation path reject
  `LMD_TYPE_INT64/UINT64/FLOAT/FLOAT64` **in every build**, before publishing
  an object. The failure is fatal (`log_error` with a distinct searchable
  prefix, then abort), because continuing would create an object that SG7
  intentionally refuses to trace.
- `heap_alloc` / `heap_calloc` / `heap_calloc_class` retain debug assertions as
  defense-in-depth and to identify the higher-level caller. Checking only the
  first two is insufficient because generated code can use the class path.
- A temporary allocation-layer counter for all four scalar tags exists during
  migration. Unlike `lambda_scalar_heap_rehome_count`, it also observes direct
  P2 allocations and therefore provides the meaningful zero gate.

Once SG2–SG6 land and the gate in SG8 passes, `scalar_heap.cpp`, its counters,
the `sys_func_registry.c` entry, and the `mir_emitter_shared.hpp:2605` import
plumbing are deleted. The all-build rejection remains permanently.

### SG2 — Public entry wrappers: complete the caller-donated-home protocol across the boxed ABI

*(User direction #1: "follow other paths, use number stack/etc.")*

The internal protocol is already caller-donation; public wrappers heap-rehome
only because the boxed ABI has no home parameter. Close that gap rather than
inventing a second mechanism. The essential rule is **transitive forwarding**:
an `Item` pointing at a donated home may be returned only to the owner of that
home. No intermediate C stack frame may substitute its own local home and then
return that Item.

Before changing signatures, write a live ABI matrix distinguishing:

- Lambda `RetItem` call paths (`FN_FLAG_BOXED_RET`);
- MIR-Direct `_b` wrappers and `fn_call_boxed_N` plain-`Item` trampolines;
- JS generated-body/public-wrapper calls;
- Python wrappers;
- top-level isolated-JS `transpile_js_*` / execution entries;
- Jube and concurrency entry/trampoline calls.

The live tree currently uses both `RetItem` and plain-`Item` interpretations,
so the phrase “every `_b` wrapper” is not precise enough without this matrix.

- **Generated ABI**: every home-capable `_b` wrapper (Lambda, JS, Python)
  gains one trailing `uint64_t* scalar_home` parameter. The epilogue's
  heap-rehome call is
  replaced by the existing adopt path (`em_adopt_scalar_item` /
  `lambda_item_adopt_scalar_home`) into that home — for JS this means the
  `incoming_scalar_home` branch at `js_mir_hashmap_scope_utils.cpp:421`
  becomes the only branch; Lambda and Python wrappers gain the same. NULL home
  aborts, exactly as `lambda_item_adopt_scalar_home` already does for a
  missing ABI home. **No silent fallback** — any fallback would resurrect the
  heap cell.
- **Result-bearing dispatch APIs make the home explicit.** Use names such as
  `fn_call_into(..., uint64_t* result_home)` and
  `js_call_function_into(..., uint64_t* result_home)` for paths that may return
  a wide scalar. The wrapper's callers are a closed, in-repo set: `fn_call` /
  `fn_call0..3` / list variant, `fn_call_boxed_N`, `js_call_function` / method /
  construct / callback invokers, the Python trampoline, top-level isolated-JS
  entries, Jube trampolines, and concurrency task starts.
- **Generated dynamic call sites** pass their per-site liveness-colored home —
  the `em_scalar_home_bind` machinery already used for direct calls, so loops
  reuse one slot per simultaneously-live result rather than per call.
- **Forwarding C helpers** accept the caller's `result_home` and forward that
  exact pointer through every tail-return chain. A direct
  `return js_call_function(...)`, `return fn_call...(...)`, proxy/method
  recursion, or callback trampoline is a forwarding boundary and may not use
  a local home. This requirement propagates through any native builtin/helper
  that can return the dynamic result to generated code.
- **Terminal C consumers** may use a stack local
  (`LAMBDA_SCALAR_HOME(name)` → `uint64_t name = 0;`) only when the returned
  Item is fully consumed before that C function returns: unboxed immediately,
  discarded, or copied with `owned_item_slot_store` into a persistent
  destination. The macro must not appear in a function that returns the Item
  or stores it in borrowed raw memory.
- **Store/discard convenience forms** should make terminal intent obvious
  where useful, so ignored callbacks and destination-owned publication do not
  encourage accidental naked Item forwarding.
- **Top-level isolated-JS result APIs** accept a caller-donated home. Their C
  caller owns that home across the `context = old_context` restoration, which
  resolves P2B without a GC scalar cell or a slot on the departing number
  stack.
- **Rejected alternatives.** *Callee donation* (retire one slot into the
  caller's number frame per call) is unbounded in dynamic-call loops — the
  precise hazard the "prevent unbounded frame donation" comment in
  `lambda_item_adopt_scalar_home` exists to block; a 10M-iteration loop would
  overflow the number stack. *A context-global return slot* is errno-style:
  valid only until the next public call, unauditable across the runtime.
  Caller donation is bounded, already the internal doctrine, and is exactly
  what `scalar_heap.cpp`'s own comment anticipates ("until that ABI can donate
  a retired home").
- **Rejected C-local forwarding.** A local `uint64_t` is not a miniature
  caller-donated protocol: once its C function returns, the Item is dangling
  before an outer wrapper can adopt it. This is banned even if the outer
  caller immediately re-homes the result.
- **C2MIR**: permanently exempt — **the legacy C2MIR path is FROZEN** (user
  decision 2026-07-22, recorded as CLAUDE.md/AGENTS.md rule 14). Its generated
  code has no watermark protocol, so its results are frame-scan-owned and
  never dangle, and no GC scalar cells are produced; it needs no ABI change,
  ever. `--c2mir` simply does not gain new features.
- **Cross-module direct calls**: the calling module's generated code binds a
  per-site home like any internal call — only the wrapper signature matters.

### SG3 — JS exceptions: grouped state struct with an owned wide-scalar slot

*(User direction #2, including the field-grouping.)*

```c
typedef struct JsExceptionState {
    bool pending;
    Item slots[2];     // slots[0] = thrown value; slots[1] = owned payload word
    char msg_buf[/* existing size */];
} JsExceptionState;   // member of js_runtime_state; macros remap
```

- `js_throw_value`: replace `lambda_item_heap_rehome(value)` with
  `owned_item_slot_store(state.slots, 1, 0, value)` — the identical 1-slot
  pattern already used for task results and mailboxes
  (`concurrency.cpp:362,950`). GC root registration stays on `slots[0]` only;
  the payload word is a value and is never traced.
- `js_clear_exception` (and any peek): return
  `owned_item_slot_read(state.slots, 1, 0, /*immortal*/false)`, which
  rematerializes wide scalars into the catcher's number frame. This is
  **required, not cosmetic**: the caught Item must not alias the state slot,
  or the next throw — even one raised and caught inside the catch block —
  would clobber a held value. Rethrow chains (e.g. iterator-close at
  `js_mir_statement_lowering.cpp:4590`) become clear → frame copy → throw →
  copied back into the owned slot, automatically safe.
- The read/rematerialization happens **before** `slots[0]` is cleared. Audit
  every clear/peek call for a live numeric context. A host/context-restoration
  boundary must use an explicit caller-home form rather than asking `push_d`
  to allocate in a missing or departing frame.
- Existing `js_exception_pending` / `js_exception_value` /
  `js_exception_msg_buf` macros (`js_runtime_state.hpp:112–114`) remap onto
  the struct; call sites outside throw/clear are untouched.

### SG4 — `Function.prototype.bind`: destination-owned payload in the function object

*(User direction #3a.)*

- **First consolidate the layout.** `JsFunction` is mirrored by
  `JsFunctionLayout`, several partial structs, and anonymous offset casts in
  `js_globals.cpp` / `js_props.cpp`. Promote one canonical definition to the
  shared module header and remove the mirrors before changing its size. All
  allocations use `sizeof(JsFunction)` from that definition. Add assertions
  for the compiled-function prefix, important field offsets, and total size.
  This is required both by repository rule 13 and because inserting a second
  Item shifts every field after `bound_this`.
- Then replace `Item bound_this` (`js_runtime_internal.hpp:79`) with
  `Item bound_this_store[2]` plus accessors
  `js_function_set_bound_this()` / `js_function_get_bound_this()` wrapping
  `owned_item_slot_store` / `owned_item_slot_read(…, /*immortal*/false)`.
- Set at bind time copies the payload **into** the function object — interior
  storage, same doctrine as array tails and JS envs: a GC object may *contain*
  scalar payload words; it just never *is* a standalone scalar cell.
- Get at call time rematerializes into the current frame before installing as
  `this`, so no interior-pointer Item escapes the object's lifetime. (With
  SG7, even an escaped one is GC-inert by construction, but the read rule
  keeps lifetimes locally obvious.)
- Eliminate direct truthiness/read tests of `bound_this.item`; bound state is
  represented by `JS_FUNC_FLAG_HAS_BOUND_THIS`, and all value reads go through
  the accessor. This includes builtin dispatch, constructor dispatch,
  `toString` checks, cloning/binding an already-bound function, and the GC
  tracer.
- GC tracing of `JsFunction` already marks `bound_this`; after consolidation,
  marking `slots[0]` when it holds a wide scalar becomes a no-op under SG7.
- Note `bind` stores the raw primitive per spec (sloppy-mode wrapping happens
  at call time), so wide-scalar `bound_this` is reachable via
  `f.bind(5e-324)` or an FFI-face u64 — covered by the tests in SG8.

### SG5 — Jube host-data API: remove the rehome entry

*(User direction #3b.)*

- Add the missing `item_slots_load` peer to the existing
  `JubeHostDataAPI::item_slots_store`, implemented with
  `owned_item_slot_read(..., /*immortal*/false)`. The live Jube API currently
  exports raw-slot store but no general raw-slot rematerializing read, so SG5
  must not claim that the complete replacement already exists.
- Delete `JubeHostDataAPI::item_heap_rehome` and
  `jube_host_data_item_heap_rehome` from `jube_registry.cpp`. Modules persist
  scalars with the owned `item_slots_store` / `item_slots_load` pair — which
  is strictly better:
  heap-rehome never protected reference types anyway (a rehomed String Item
  held in raw module memory is still untraced; the G1 rooting ledger governs
  those), so scalar persistence — its only real function — is the slot API's
  job.
- Native modules are source-only distribution (`Lambda_Native_Module_Design`),
  but deleting this middle struct field shifts the following `map_new` and
  `function_new` entries. Bump/update the Jube host-service API/version and all
  `JUBE_HOST_DATA_API_*_SIZE` checkpoints, then rebuild and migrate every
  in-tree initializer/client. Reference Items held in module storage still
  follow the separate root-registration ledger.

### SG6 — Builtin double and mutable-scratch sweep

#### SG6A — 42 ordinary P2A sites

For each site, record one of three consumers before editing it:

1. immediate unbox/consumption in the current frame;
2. immediate destination-owned store (`js_property_set`, array/tuple/VMap
   store, `owned_item_slot_store`);
3. return/forwarding path covered by SG2's transitive caller home.

Only then replace the 3-line alloc/store/encode pattern with `push_d(value)`.
Behavior is identical for inline doubles; out-of-band doubles land on the
number stack and must have one of the three proven owners above. This removes
instant-garbage allocations without treating lifetime as a mechanical detail.
(Rule 13: `push_d` is the shared helper; do not add another boxing helper.)

#### SG6B — 2 isolated-context P2B sites

Do not call `push_d` in the context that is about to be restored. Extend the
top-level isolated-JS entry ABI to receive `uint64_t* result_home`; while the
JS context is still active, adopt `final_result` into that caller-owned home,
then restore `context`. All C callers of those entry APIs donate storage whose
lifetime covers their use of the returned Item.

#### SG6C — mutable scratch rings

Delete `date_buf` and `gt_buf`. The Date property path boxes with `push_d` and
immediately stores into its destination; the `getTime` return follows SG2.

### SG7 — GC marker fast path + debug verification

- Change `item_to_ptr` to receive `gc_heap_t*` (or perform the scalar-tag arm in
  `gc_mark_item`, where `gc` is already available). Tags `LMD_TYPE_INT64_` /
  `UINT64_` / `FLOAT_` return NULL unconditionally in release. In debug, decode
  the pointer first and assert `!is_gc_object(gc, ptr)`, then return NULL. The
  current `item_to_ptr(uint64_t)` has no `gc` argument, so the proposal must
  not pretend the verification can call `is_gc_object` without this signature
  or placement change.
- Implement mandatory debug poisoning of reclaimed side-number-stack slots on
  every watermark restore. A dispatch-helper migration miss must read poison
  instead of silently stale data; this is a deliverable, not an “if already
  covered” option.
- Enable the marker fast path only after the all-build allocation rejection is
  active and the SG8 allocation-layer zero gate has passed.

### SG8 — Order, gate, deletion

1. **Inventory and instrumentation** — write the ABI matrix; classify all P2
   consumers; add the allocation-layer scalar-tag counter; migrate/delete the
   existing fallback test that intentionally increments
   `lambda_scalar_heap_rehome_count()` before requiring a zero suite result.
2. **SG3 → SG4 → SG5** — exception owned slot; canonicalize the `JsFunction`
   layout and then add its owned slot; remove/migrate the Jube fallback.
3. **SG6A + SG6C** — convert only sites with a proven terminal owner or an
   already-correct return path; remove the two mutable scratch rings.
4. **SG2 + SG6B** — land the transitive ABI change and isolated-context result
   migration together. Rebaseline MIR emission budgets
   (`test/mir/mir_budgets.json`) per MT7.
5. **Migration gate** — `lambda_scalar_heap_rehome_count()` shows zero deltas
   for the INT64, UINT64, and FLOAT/FLOAT64 families, and the allocation-layer
   counter shows zero deltas for all four scalar tags, across focused tests,
   `test-lambda-baseline`,
   `test-radiant-baseline`, Test262, and `make node-baseline`. The old direct
   fallback-allocation test has already been replaced with invariant/owned-slot
   coverage, so it cannot invalidate the gate by construction.
6. **Finalize SG1** — enable the permanent all-build rejection in every GC
   object allocation route; add lint bans on scalar-typed GC allocation and
   the old alloc/store/encode pattern; delete `scalar_heap.cpp`, counters,
   registry entry, and import plumbing.
7. **Finalize SG7** — enable the marker fast path and debug zone verification;
   keep number-stack poisoning enabled in debug builds.
8. **Documentation synchronization** — update at least
   `Lambda_Design_Stack_API.md`, `Lambda_Impl_Numbers.md`,
   `Lambda_Impl_Sized_Int (done).md`, `Lambda_Type_Int_Sized.md`,
   `Lambda_Impl_Stack_Frame_Py.md`, `Lambda_Issue_Scalar_Lane.md`, and
   `Lambda_Design_MIR_Emission_Test.md` so no durable record still prescribes
   or expects a GC scalar cell.

**New tests** (each currently exercises a P1/P2 path):

- subnormal (`5e-324`) returned through dynamic dispatch and a JS callback;
- subnormal returned through at least two tail-forwarding C helpers;
- two simultaneously-live returned wide scalars, proving homes do not alias;
- nested/reentrant callback returning a subnormal;
- isolated `js_main` / require result survives context restoration and forced
  GC in the surviving context;
- `throw`/catch/rethrow of a subnormal and of an FFI-face u64;
- caught wide-scalar value survives a subsequent throw inside the catch block;
- `f.bind(5e-324)` then call, including forced GC before the call;
- `BigInt64Array` element egress through a callback;
- cross-module `INT64` and `UINT64_MAX` returns;
- Jube/host result persistence through the owned-slot API;
- death/invariant tests proving every GC allocation route rejects scalar tags.

## 4. Impact estimate

| Piece | Size | Risk |
|-------|------|------|
| SG6A/SG6C sweep | 42 classified P2 edits + 2 scratch-ring removals | low after per-site ownership classification |
| SG6B isolated-context result | 2 sites + top-level caller-home API threading | high — crosses context restoration |
| SG3 exception struct | ~80 LOC + clear/peek context audit | low–medium — localized, pattern exists |
| SG4 layout + bind accessors | canonical layout cleanup + allocation/accessor/tracer sweep | high until duplicate layouts are removed |
| SG5 Jube API replacement | add raw-slot load, remove rehome, version/size-checkpoint bump, module migration | medium — source rebuild is required and following function-pointer offsets move |
| SG2 ABI threading | 3 transpilers + result-bearing dispatch APIs + transitive native-helper/tail-return sweep | **the** risk item; raw references number in the hundreds, so scope is determined by the ABI census, not an assumed helper count |
| SG1/SG7 finalization | deletions + all-build guards + asserts + poison + lint | low only after the allocation-layer gate passes |

## 5. Open questions

- **OQ1 — blocking checklist**: enumerate every result-bearing dispatch helper
  and direct tail return. Each row records its caller-owned home, whether it
  forwards/stores/consumes, and the focused subnormal test that proves the
  lifetime. SG2 is incomplete while any row is unclassified.
- **OQ2**: `start`/await result edge — `task->result` is already a 1-slot
  owned store (`concurrency.cpp:950`); confirm the resume path reads through
  `owned_item_slot_read` (expected yes; verify).
- ~~OQ3~~ **RESOLVED 2026-07-22**: C2MIR is frozen as-is (CLAUDE.md/AGENTS.md
  rule 14) — permanently exempt, nothing to revisit.
- **OQ4**: whether `js_get_exception_value`-style peeks (if any exist besides
  clear) also need the rematerializing read, and whether any clear/peek can run
  after context restoration. Such a path needs an explicit caller-home form.
- **OQ5 — ABI census**: reconcile the live `RetItem` and plain-`Item` boxed
  trampoline interpretations before signature edits. Record which distinction
  is semantic and which comments/flags are stale.
- **OQ6 — top-level result lifetime**: enumerate every caller of the two
  isolated-JS entry families and prove the donated home outlives all use of the
  returned Item. A convenience wrapper may use a C local only when that local
  belongs to the outer caller, not an intermediate entry helper.

## 6. Definition of done

The invariant is complete only when all of the following are true:

1. No object-zone or bump/class allocation can publish a scalar GC type tag in
   debug or release.
2. No generated/public/host return can expose a pointer to a completed
   activation, an intermediate C local, a restored context, or mutable scratch
   storage.
3. Persistent scalar destinations own their payload word and reads
   rematerialize or adopt into a live caller home.
4. The allocation-layer scalar-tag counter and old rehome counters remain zero
   across the complete gate, with no test excluded for intentionally creating
   a scalar cell.
5. Debug watermark restore poisons reclaimed numeric homes, and the focused
   nested/tail/reentrant/context-restore tests pass under forced GC.
6. The collector ignores scalar tags only after items 1–5 are satisfied.
7. All durable design, implementation, and MIR-emission documents describe the
   same no-scalar-cell end state.

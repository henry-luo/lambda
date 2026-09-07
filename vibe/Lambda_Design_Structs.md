# Lambda Design: Runtime Struct Authority — The Five That Matter

> **Status: DRAFT for user ratification — 2026-09-07.** This is the design
> record for the five highest-yield items of
> [`Lambda_Proposal_Struct_Clean_Up.md`](Lambda_Proposal_Struct_Clean_Up.md)
> (the parent proposal; its census and rules SCU1–SCU6 are assumed). It
> extends the parent's `SCU` ledger series with **SCU7–SCU17** and opens
> **SCUO1–SCUO5**. Nothing here revises a formal ruling; every item
> *implements* one that already stands. The one item that would need a
> revision (the contiguous `Shape` layout, parent §4.4/§4.7) is deliberately
> out of scope and parked as SCUO1.
>
> **Spec linkage.** Item 1 implements **D2.6.1**, **D3.4.6** and
> **D2.4.1**; item 2 implements **D2.4.1**, **D5.2.1v3** and vibe **RV10**;
> item 3 implements **D5.4.1–D5.4.2** and vibe **RG0–RG2**; item 4 implements
> **D1.9**; item 5 implements **D1.4v3** and **D6.4.1**. Guardrails that
> must survive every item: **SCU2** (semantic type and physical
> representation stay separate), **SCU4** (shared storage never erases
> language meaning), **D3.4.1** (the `ShapeEntry` chain remains normative
> until revised).
>
> **Scope.** Native Lambda runtime (`lambda/core`, `lambda/runtime`,
> `lambda/input`, `lambda/io`). LambdaJS and Radiant are compatibility gates.
> All anchors below were re-resolved against `master` on 2026-09-07.

## 0. Why these five, and why in this order

The parent proposal lists nine stages. Re-reading it against the tree on
2026-09-07 shows every finding except P1.1 still live. The five items below
are the ones whose payoff is a *bug family* or a *blocked design*, not a
tidier census; the rest are churn for churn's sake right now.

| # | Item | Parent stage | Why it is first-tier | Formal ruling it implements |
|---|------|--------------|----------------------|-----------------------------|
| 1 | One total storage-descriptor resolver, stored on the shape | P3.1, P3.4 | Five runtime bugs in one month were the same defect: a consumer derived the physical lane from a `TypeId` instead of the full contract | D2.6.1, D3.4.6 |
| 2 | One `FnAbiContract` for entry/return/companion facts | P1.4, §6.2 | The D8.1.1 satellite/auto-tier work in flight keeps re-deciding call transport per site; five authorities is how tier-mismatch bugs are born | D2.4.1, D5.2.1v3, RV10 |
| 3 | `EvalContext` is the sole owner; `Runtime` stops mirroring | P6 | JS threading, Radiant page isolates and the scheduler all assume one owner per isolate resource; the bidirectional copy is a latent double-free | D5.4.1 |
| 4 | Parser working sets grow from the document | P2.3 | Three silent-truncation bugs and an 800 KB stack frame per parser; a day of work | D1.9 |
| 5 | Retire `RetItem` | P1.2 | 41 wrapper twins and two dispatch branches to emulate a protocol the runtime already has | D1.4v3, D6.4.1 |

Recommended landing order is **4, 5, 1, 2, 3**: the two cheap isolated
items first so they are banked, then the bug-family killer, then the two
that touch active tuning ground. Items 4 and 5 need no coordination with
anyone; items 1–3 each get their own branch and their own baseline run.

What is **not** here and why:

- The contiguous `Shape`/`ShapeField[]` layout (parent §4.4). Requires
  D3.4.1→v2 and D3.4.2→v2 first (rule 17). SCUO1.
- One `Item` typedef and `RecordStorage`/`SequenceStorage` (parent §5).
  Highest churn in the whole proposal, lowest bug yield. Deferred.
- `MirTranspiler` / `VarEntry` / `NameEntry` splits (parent §9). They live
  in the file the D8.1.1 tuning is editing daily; splitting now guarantees
  merge conflicts and no bug is waiting on it. Instead the parent's P0.3
  ratchet applies from today: **no new fact field on `NameEntry`,
  `VarEntry` or `MirTranspiler` without naming its canonical owner in the
  commit**. SCUO4.
- `TypeType` deletion (parent §4.1). Worth pulling forward on its own
  because the `T?`-carries-`LMD_TYPE_TYPE` trap has bitten twice, but it
  is a type-graph change, not a struct-authority change. SCUO5.

---

## 1. One total storage-descriptor resolver, stored on the shape

> **Status: LANDED 2026-09-07.** SCU7–SCU10 implemented. The total resolver
> `lambda_lane_storage_desc_for()` lives in core (`lambda-data.cpp`) with the
> promoted contract walks (`lambda_type_accepts_error/null`,
> `lambda_type_nullable_lane_base`); it carries a `native` bit so the two old
> predicates (`lambda_type_lane_storage_desc`, `shape_entry_uses_native_lane`)
> are exact projections and no JIT/array caller changed meaning.
> `ShapeEntry::storage` is filled by `shape_entry_set_type()` at every
> constructor/retag site (repaired and logged on first read if a site is
> missed); widths come from the descriptor everywhere; the collector decodes
> by kind through `lambda_shape_entry_lane()`; `ShapeBuilder` and the shape
> pool have no field cap (heap probe past 32 KiB). Two latent bugs surfaced
> and were fixed at the root: `int[]?` fields classified as a `type` pointer
> lane (the pointer-lane list pre-empted TB1), and the resolver's 9-byte
> `TypedItem` width. Tests: `LaneStorageResolverTests` in
> `test/test_mark_editor_gtest.cpp`. Gates: Lambda baseline 4141/4141, GC
> stress 107/107, mark-editor 42/42.

### 1.1 What the rulings already say

D2.6.1: shapes carry an immutable `LaneStorageDesc` from **one shared
descriptor resolver** for MIR, map layout, arrays and guests. D3.4.6: the
descriptor is derived from the **full `Type*`**, and changing a field's
*contract* re-derives layout. D2.4.1: the semantic contract (`Type*`) and
the planned representation are separate authorities; the second is derived
from the first, never recovered from a `TypeId` or a register class.

### 1.2 What the tree actually does

The resolver exists and is **partial**, and the descriptor is **not
stored**. Concretely:

- `LaneStorageDesc` is declared at `lambda/lambda.h:1006` and the resolver
  `lambda_type_lane_storage_desc()` at
  `lambda/runtime/type_contract.cpp:525`. It answers only for the
  native-lane cases (`int`, `bool`, `float`, integer `NumSized`, nullable
  `int64`/`uint64`) and returns `false` for `any`, `null`, `error`, type
  values, non-nullable wide ints and every pointer family. Fifteen callers.
- Because the resolver says "no" for most contracts, four **parallel
  deciders** answer the same question from less information:
  - `type_field_storage_type_id()` (`lambda/core/lambda-data.cpp:1444`,
    10 callers) re-implements the nullable unwrap and returns a `TypeId`.
  - `shape_entry_storage_type_id()` + `type_info[].byte_size`
    (`lambda-data.hpp:505`, 26 callers) turn that `TypeId` into a width;
    the shape pool sums widths the same way at `shape_pool.cpp:95,197,217`
    and `build_ast.cpp:4718,5067`.
  - `shape_entry_uses_native_lane()` (`lambda-data.cpp:1543`) is a third
    copy of the nullable/union unwrap, written so that "a ShapeEntry and an
    array boundary cannot disagree about `T?`" — by duplicating the logic
    rather than sharing it.
  - ArrayNum element kind is decided ad hoc: `num_sized_to_elem_type()`
    (6 callers) plus inline ternaries such as `transpile-mir.cpp:17423`.
  - `lambda_canonical_rep()` (47 callers) answers the MIR-side version of
    the question (`ValueRep`) from its own switch.
- `ShapeEntry` (`lambda-data.hpp`) has no descriptor field. Every read,
  write, GC trace and MIR lowering **recomputes** the lane from
  `ShapeEntry::type` through whichever decider that code path happens to
  call. D3.4.6's "shapes carry" is not implemented.
- `ShapeBuilder` (`lambda/core/shape_builder.hpp:23`) drafts fields as
  `(const char*, TypeId)` pairs in two fixed 64-slot arrays. A shape built
  through it **cannot express** `int?` or `int64` at all — the builder
  drops the contract before the pool ever sees it — and field 65 is
  silently refused (`shape_builder.cpp:46`).

The bug family this produced, all fixed individually in the last month:
the GC any-lane field bug (2026-08-26, GC read the storage lane instead of
the contract), the GC null-lane UAF (2026-09-07, D4.3.4: containers in
null-typed shaped fields at unaligned offsets were never traced), the
nullable-bool lane fix in T21-1, optional-map contract blindness (`N?`
reports TYPE not MAP), and the MIR carrier-invariant violation for datetime
literals. Each fix patched one decider. The next one will patch another.

### 1.3 Rulings

**SCU7 — The resolver is total.** `lambda_type_lane_storage_desc()` (or
its renamed successor) answers for **every** `Type*`, never returns
`false` for a well-formed contract. The result for `any`, untyped, unions
that are not `null | T`, type values and error-admitting contracts is
`LANE_STORAGE_ITEM` with `byte_size = sizeof(Item)` (or the 16-byte
`TypedItem` where D3.4.1 says so). Pointer families resolve to
`LANE_STORAGE_POINTER` with their `TypeId` in `value_domain`. Non-nullable
`int64`/`uint64` resolve to `LANE_STORAGE_ITEM` (they box; D2.2.3). A
malformed or null `Type*` is a **programming error** that logs and aborts
in every build (D1.9), not a `false`.

**SCU8 — Every other decider is a projection of the resolver.**
`type_field_storage_type_id()`, `shape_entry_uses_native_lane()`, the
shape-pool byte-size sums, the ArrayNum element-kind choice and
`lambda_canonical_rep()`'s storage-relevant arm are rewritten as thin
functions over the descriptor (`desc.kind`, `desc.byte_size`,
`desc.value_domain`, `desc.nullable`). They keep their names during
migration and are deleted once callers take the descriptor directly.
Adding a new `switch (type_id)` that decides width, lane kind or element
kind anywhere outside the resolver is a review reject.

**SCU9 — The descriptor is stored on the shape field.** `ShapeEntry` gains
one `LaneStorageDesc storage` (compact form, see SCUO2) filled at the moment
`ShapeEntry::type` is set and never recomputed. Shape copies, transitions
and pool interning copy it with the entry (D3.4.4v2 already requires the
same for `name_id`). Readers, writers, the collector and MIR direct access
consult `entry->storage`; none re-derives from `entry->type`. This is the
sentence in D3.4.6 that says "shapes carry", made literal, and it is the
reason the GC and the JIT can no longer disagree: they read the same byte.

**SCU10 — Builders draft full contracts.** `ShapeBuilder` holds an
`ArrayList` of `ShapeFieldDraft { NameId name; Type* contract; uint8_t
key_kind; }`, allocated from the pool's arena, with **no field-count
limit**. `shape_builder_add_field()` takes a `Type*`, and the legacy
`TypeId` overload becomes a one-line adapter that looks up
`type_info[id].type`. The builder never computes width or offset; the pool
computes both from the resolver at `final()`.

### 1.4 What does not change

- The `ShapeEntry` **chain** stays; D3.4.1 is untouched. SCU9 adds a field
  to the existing entry; it does not introduce `ShapeField[]`.
- D3.4.2's identity (name, `TypeId`) stays. Once SCU9 lands, identity
  *should* include the descriptor (a `int` field and a `int?` field have
  the same `TypeId` and different lanes) — that is the D3.4.2→v2 revision
  the parent's §4.7 already calls for. SCUO1 carries it; until then the
  pool's existing `int?`-aware keying (Nullable §6) is the interim.
- `LaneStorageDesc` keeps `semantic_contract` as a **non-owning** back
  pointer where the compact form cannot; it never becomes a second
  authority (SCU2).

### 1.5 Acceptance

- Grep gate: one function body contains a `switch` on `type_id` that
  assigns a `LaneStorageKind`; `type_info[...].byte_size` is read from
  the resolver and nowhere else in `lambda/core`, `lambda/runtime`.
- A byte-level cross-path test: for every contract in a fixture list
  (`int`, `int?`, `bool?`, `float`, `int64`, `int64?`, `i32?`, `string`,
  `map?`, `T | null`, `T[]?`, `any`, an object with an `int?` field at an
  unaligned offset) build a map three ways — literal, `ShapeBuilder`, a
  runtime transition — and assert the JIT direct field read, the runtime
  `map_shape_field_to_item`, and the GC trace descriptor agree on offset,
  width and lane kind.
- `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_POISON_FREED=1` over the null-lane UAF
  reproduction stays green.
- Release-build (`make release`) AWFY subset within noise of the archived
  control; one benchmark run at a time.
- `make test-lambda-baseline` 100 %.

---

## 2. One `FnAbiContract`

> **Status: LANDED 2026-09-07 (SCU11 as `FnReturnAnalysis`).** The published
> `FnReturnAnalysis` (lanes, shape, companion) inside `FnVariantAnalysis` is
> the contract; no new struct was minted. `MirFunctionPlan.abi` and
> `JitCallMetadata.abi` reference it, and `em_plan_bind_return()` is the only
> writer of a plan's `return_shape`/`companion` (plus `em_plan_bind_hosted_pair`
> for Jube guest frames); the four emission sites that assigned them are gone.
> `MirScalarReturnMode` is deleted in favour of the one `ScalarReturnClass`
> (the two identity converters with it); the file-local
> `FunctionReturnLaneKind` is promoted to the shared `MirReturnLaneKind`; the
> body lane/scalar derivation that was spelled three times in
> `transpile-mir.cpp` is one pair of helpers (`lambda_body_return_lane`,
> `lambda_body_scalar_class`). SCU12 (`FnEffects`) and SCU13 (`Function` bits)
> are unchanged: `FnEffectSummary` already sits beside the return analysis and
> the runtime bits keep their pinned positions. Gates: tier sweep 765 scripts,
> mismatch 0; MIR emission ratchets 77/77 and 21/21; satellite goldens 12/12 in
> both tiers; Lambda baseline 4141/4141.

### 2.1 What the rulings already say

D5.2.1v3 fixes the four return shapes and the companion transport; RV2
says the shape is a **pure function of the declared signature**, never
inferred per call site; RV10 says there is **one convention descriptor,
consumed everywhere**. D2.4.1 separates the semantic contract, the planned
representation and the emitted `MirValue`. D6.4.1 fixes the sys-func
boundary at one boxed `Item`.

### 2.2 What the tree actually does

`FnReturnAnalysis` (`lambda/runtime/ast-core.hpp:1238`) carries a comment
calling itself "the single source of truth for how this entry returns. No
emitter site may recompute it locally — that divergence is the v27 havlak
wrong-answer bug class." It is one of **five** places the same facts live:

| Holder | Fields duplicating the decision | Anchor |
|--------|--------------------------------|--------|
| `FnReturnAnalysis` | `shape`, `companion`, `error_lane`, `normal`, `error` | `ast-core.hpp:1238` |
| `MirFunctionPlan` | `entry_kind`, `return_shape`, `companion`, `scalar_return_mode`, `scalar_home_lane_mask` | `mir_emitter_shared.hpp:392` |
| `JitCallMetadata` | `return_shape` ("mirrored from the callee's FnReturnAnalysis"), `normal_result`, `error_result`, `scalar_home_lane_mask` | `sys_func_registry.h:170` |
| `Function` flags | `mir_public_return_shape:2`, `returns_ret_item`, `requires_scalar_result_home` | `lambda.h:1322` |
| `SysFuncInfo` | `c_ret_type`, `can_raise`, `success_type`, `may_return_error` | `sys_func_registry.h` |

And a sixth that is not a holder but a **recomputation**: the file-local
`enum FunctionReturnLaneKind` at `transpile-mir.cpp:1545`, combined with
`MirScalarReturnMode`, is fed to `mir_body_returns_pair()` which derives
the shape *again* from `(lane_kind, scalar_mode)` — in the same file whose
comments say deriving it twice is the havlak bug. `begin_function_epilogue`
takes those two loose values as parameters rather than the plan.

Why this is first-tier now rather than later: the D8.1.1v8/v9 satellite
work lands per-callee entries (`_b` CW33 adapters, typed-var satellites,
member entries through `find_func` rather than `jit_gen_func`). Every one
of those is a call-transport decision. With five holders, each new entry
kind is five edits and one missed edit is a silent wrong answer in one
tier only — exactly what the interp-sweep exists to catch after the fact.

### 2.3 Rulings

**SCU11 — One immutable `FnAbiContract` per callable, produced once.**

```c
typedef struct FnAbiContract {
    FnEntryKind          entry_kind;
    FnReturnShape        return_shape;    // RV1 shape 1–4
    FnCompanionTransport companion;       // RV10a/RV12
    FnReturnLane         normal;          // ValueRep of lane 1
    FnReturnLane         error;           // ValueRep of lane 2 when shape 4
    uint32_t             flags;           // C-reachable, public, method, …
} FnAbiContract;
```

Return analysis is its **only producer**. `MirFunctionPlan` and
`JitCallMetadata` hold `const FnAbiContract*`, not copies. `SysFuncInfo`
gets its contract computed **once at registry initialization** from
`c_ret_type`/`can_raise`/`return_type`, so the 900-row registry table keeps
its spelling and gains one pointer. `MirScalarReturnMode` folds into
`normal.rep` (it is the lane-1 `ValueRep`, nothing more).
`FunctionReturnLaneKind` is deleted; `mir_body_returns_pair()` and
`begin_function_epilogue()` take the contract. The `FnErrorLane` field
goes wherever `return_shape` already decides it.

**SCU12 — Effects are not ABI.** `JitCallEffects` (already in
`JitCallMetadata`) is promoted to a standalone `FnEffects` that owns GC,
re-entry, exception, number-stack, suspension and argument-capture facts.
Call sites consume both; that is not a reason to merge them (parent §6.2).

**SCU13 — `Function` keeps its two public bits, for now.** The runtime
`Function` record is GC and MIR ABI with pinned offsets. Its
`mir_public_return_shape:2` stays because generated code reads it at a
fixed offset; it is documented as a **cached projection** of the
contract, written from the contract at publication and nowhere else.
`returns_ret_item` dies with item 5. The `FunctionCode`/`FunctionValue`
split (parent §6.3, P7) is not part of this item.

### 2.4 Acceptance

- Grep gate: `FnReturnShape` is assigned in exactly one function
  (`fn_return_analysis_*`); `MirScalarReturnMode` and
  `FunctionReturnLaneKind` have no definition.
- `make interp-sweep` mismatch = 0 across the 751-script partition.
- MIR emission ratchet (MT7) unchanged at 0 % slack.
- The satellite goldens (diviter, richards, deltablue, havlak, json2,
  navier_stokes, nbody2) pass in **both** tiers.

---

## 3. `EvalContext` owns; `Runtime` controls

> **Status: LANDED 2026-09-07 (SCU14, SCU15).** `Runtime.heap`,
> `name_pool`, `type_list`, `scheduler` and `js_bootstrap_context` are
> deleted. The canonical `EvalContext` is the sole owner; `Runtime` reaches
> the four resources through `runtime_heap()/runtime_name_pool()/
> runtime_type_list()/runtime_scheduler()` (reads never create the context)
> and `runtime_set_*()` (a write materializes it). All 108 sites across the
> runner, main, the JS require path and event loop, Radiant (events, script
> runner, layout, window) and DOM were converted mechanically.
> `runtime_context_bind_retained` copies from the canonical owner (a self-copy
> for every Radiant/DOM entry, which all bind `runtime_get_eval_context`);
> `runtime_context_publish_owners` copies a non-canonical realm's tuple into it
> (test262 batch realm, one-shot guest heap) and is otherwise a no-op. The
> bootstrap context was always the canonical context (`load_js_module` binds
> `runtime_get_eval_context` before recording it), so its adoption and the two
> cleanup branches were dead and are gone. The migration landed as one change
> rather than one resource at a time because every reader already went through
> two functions once the accessors existed. Not done: composition over
> inheritance (SCUO3). Gates: REPL 40/40; concurrency 18/19 with the same
> pre-existing `RuntimeGlobalsConcurrency` failure (and the stress case now
> fails cleanly instead of spinning, matching pristine HEAD); 33 Lambda and 22
> JS goldens; Radiant baseline 7984/8346 with the one "failure" being a link error in the full test build (fixed by moving `runtime_get_eval_context` into runtime-state.cpp) after which DOM UI Integration passes 119/119; Lambda baseline 4141/4141.

### 3.1 What the rulings already say

D5.4.1: one canonical long-lived `EvalContext` per isolate, exactly one
sanctioned TLS root, no save/bind/restore. D5.4.2: the JIT-visible
`Context` prefix is frozen; state lives in lazily allocated capsules.
Vibe RG1: `Runtime` is a controller.

### 3.2 What the tree actually does

`Runtime` (`lambda/runtime/transpiler.hpp:80`) still owns `Heap* heap`,
`NamePool* name_pool`, `ArrayList* type_list`, `LambdaScheduler*
scheduler`, plus `EvalContext* eval_context` **and** `EvalContext*
js_bootstrap_context`. `EvalContext` (`lambda-data.hpp:105`) owns the same
four. They are kept in sync by hand, in both directions:

- `runtime_context_bind_retained()` copies `runtime → owner`
  (`runtime-state.cpp:48`); `runtime_context_publish_owners()` copies
  `owner → runtime` (`runtime-state.cpp:61`).
- Nineteen reads of `runtime->heap`, fourteen of `runtime->name_pool`,
  twenty of `runtime->type_list`, fifteen of `runtime->scheduler` bypass
  the context.
- LambdaJS writes the mirror **directly**: `runtime->name_pool = dynamic`
  at `js_mir_entrypoints_require.cpp:133`, `runtime->type_list = …` at
  `:1118`, nulls all three at `:349`, and `runtime->scheduler =
  context->scheduler` at `js_event_loop.cpp:1387`. `main.cpp:430` nulls
  them at shutdown. `js_bootstrap_context` has 17 uses.

Nothing is crashing today because the sync calls are placed carefully.
That is the definition of a latent double-free (parent R7): the first
refactor that creates a context without calling both sync functions in the
right order frees the heap twice or uses a null name pool. It also blocks
what is queued behind it: JS threading (JT1–JT7) needs per-isolate owners,
Radiant page isolates (RC1) need to construct an `EvalContext` and have it
*be* the isolate, and the scheduler capsule cannot be lazy while `Runtime`
holds a raw pointer to it.

### 3.3 Rulings

**SCU14 — `EvalContext` is the sole owner of heap, name pool, type list
and scheduler.** `Runtime` keeps loaded scripts, the module registry,
configuration flags, DOM/UI hooks, MIR-cache counters and **one**
`EvalContext* eval_context`. The four mirrored fields are deleted along
with `runtime_context_publish_owners()`. During migration a set of inline
accessors (`runtime_heap(rt)` → `rt->eval_context->heap`, etc.) replaces
the field reads so the change is mechanical; they are deleted when the
last caller takes the context. The "retained across evaluations" property
that justified the mirrors moves with the ownership: the `EvalContext` is
created at `runtime_init`, allocates its heap lazily on first evaluation,
and lives until `runtime_cleanup`.

**SCU15 — One context per isolate means no bootstrap context.**
`js_bootstrap_context` is retired. JS semantic state is already a capsule
(`EvalContext::js_state`, D5.4.2); bootstrap runs in the isolate's own
context with that capsule initialized on first use. The direct
`runtime->name_pool = dynamic` rebinding in the JS require path becomes an
operation on the context (`eval_context_rebind_name_pool` or an explicit
capsule swap), with its invalidation boundary stated at the call.

**Migration order (one resource at a time, R7).** `scheduler` first
(fewest readers, already a capsule candidate), then `type_list`, then
`name_pool`, then `heap` last because the GC ABI reads its offset. Each
step: add accessor → convert readers → delete field → run the gate. Then
`js_bootstrap_context`.

### 3.4 What does not change

- `EvalContext : Context` inheritance stays. The parent proposes
  composition (`EvalAbiContext abi` at offset 0). The frozen prefix is
  already at offset 0 and asserted; composition would rewrite every
  `context->` access for no behavioural gain. SCUO3 records it.
- `input_allocation_context` (`lambda/io/input-allocation-context.h:18`)
  stays as the one scoped IO allocation selector (parent §7.3). The
  legacy `Context* input_context` is no longer declared in the core
  headers; P6.4 is effectively closed and needs no work here.

### 3.5 Acceptance

- Grep gate: `runtime->heap`, `runtime->name_pool`, `runtime->type_list`,
  `runtime->scheduler`, `js_bootstrap_context` each 0 hits outside the
  deletion commit.
- GC/rooting suite, task scheduler tests, module/import tests, repeated
  evaluation (REPL) tests, JS bootstrap and DOM integration, Radiant
  `test-radiant-baseline`, all green.
- ASan run of the JS require path and the Radiant callback path
  (release build per the ASan-init deadlock note).

---

## 4. Parser working sets grow from the document

> **Status: LANDED 2026-09-07.** SCU16 implemented: `SourceTracker` grows its
> line index from the document (memtrack-owned, geometric growth);
> `MarkupParser` keys link definitions by normalized label in a `hashmap`
> with Input-arena strings and no width caps; `YamlParser` keeps anchors in a
> `hashmap` with arena-owned names (which also retired the `static char[256]`
> a nested node parse could clobber). Size budgets are pinned by
> `static_assert` (`InputContext` < 1 KiB, `MarkupParser` < 16 KiB,
> `YamlParser` < 256 B). Three regression cases live in
> `test/test_input_roundtrip_gtest.cpp` (`InputWorkingSetTests`). Gates:
> input baseline 769/769, Markdown spec 1335/1335, roundtrip 42/42.
> Root cause surfaced on the way: `block_link_def.cpp` left a stale title
> span when a separate-line title turned out invalid, so the URL-only add
> received a garbage title length; the 512-byte buffer had masked it.

### 4.1 What the ruling already says

D1.9: malformed input is an error, never UB; fail closed. SCU5 (parent):
no parser instance embeds storage sized for the maximum document.

### 4.2 What the tree actually does

| Site | Embedded capacity | On overflow | Anchor |
|------|-------------------|-------------|--------|
| `SourceTracker::line_starts_` | 100,000 × `size_t` = 800 KB | `log_warn`, then context extraction (`extractLine`) is empty or wrong for every later line; the incremental line counter itself stays right | `source_tracker.hpp:16,25`, `source_tracker.cpp:35` |
| `InputContext::tracker` | embeds the above **by value** | 18 parsers stack-allocate `InputContext ctx(...)`: an 800 KB frame each | `input-context.hpp:29` |
| `MarkupParser::link_defs_` | 256 × `LinkDefinition` (`char[256]+[1024]+[512]`) ≈ 459 KB, on top of `InputContext` | `return false` at `log_debug` level: the 257th reference link silently renders as literal text | `markup_parser.hpp:126,151`, `markup_parser.cpp:648` |
| `LinkDefinition` strings | fixed | `strncpy` truncates a 1,025-byte URL silently | `markup_parser.hpp:50` |
| `YamlParser::anchors` | 256 × (`char[256]` + `Item`) ≈ 67 KB | `if (>= 256) return;` — a later `*alias` resolves to nothing, the document is **wrong** with no diagnostic | `input-yaml.cpp:50,64,279` |

The total fixed footprint of a `MarkupParser` is 1.26 MB before the first
byte of document is read. The batch-mode stack-overflow hazard on the
exec-recovery ledger (H1) makes the stack-allocated 800 KB frames a
correctness concern, not only a memory one.

### 4.3 Rulings

**SCU16 — Document-proportional parser state is allocator-owned.**

- `SourceTracker` line index: a growable array allocated from the
  `Input`'s pool (lifetime = the parse), built lazily on first
  line/column query as today. No cap. Allocation failure surfaces as a
  parse error.
- `MarkupParser` link definitions: a `HashMap` keyed by the normalized
  label, values holding arena-owned `Str` for url and title (no fixed
  widths). Duplicate labels keep first-wins as CommonMark requires.
- `YamlParser` anchors: a `HashMap` from arena-owned name to `Item`.
  Redefinition overwrites (current behaviour); an unresolved alias keeps
  resolving to `null` with a debug log (current behaviour, unchanged here).
  Anchor names are copied to the Input arena, which also retires the
  `static char[256]` that a nested node parse could clobber.
- `InputContext` holds the tracker **by value still** (it is small once
  the array is external) — the point is that `sizeof(InputContext)` no
  longer depends on any document limit.

No `if (count >= N) return` guarding a document-count array remains in
`lambda/input`. Budgets: `sizeof(MarkupParser) < 16 KiB`,
`sizeof(InputContext) < 1 KiB`, `sizeof(YamlParser) < 256 B`, each pinned
by a static assert so regressions fail to compile.

### 4.4 What does not change

- The two `SourceLocation` types (`input/parse_error.hpp:13`,
  `runtime/lambda-error.h:164`) and the `SourceSpan`/`SourceIndex`
  unification (parent §8.1) are **not** in this item. They are a
  diagnostics-model change with a different validation surface; this
  item is only about capacity.

### 4.5 Acceptance

- Regression inputs under `test/input/`: a 150,000-line text file whose
  last-line error reports the right line; a Markdown file with 300
  reference definitions and a 2 KB URL; a YAML file with 300 anchors all
  aliased. Expected outputs committed alongside (rule 8).
- Static-assert size budgets above.
- `make test` input/format suites green; no timing gate needed.

---

## 5. Retire `RetItem`

> **Status: LANDED 2026-09-07.** SCU17 implemented in one change: the ~50
> `RetItem` functions return `Item`; `ri_err(e)` became `err2it_or_error(e)`
> (the one null-safe spelling, added beside `err2it` in both headers);
> 34 registry rows flipped to `C_RET_ITEM`; the 41 `_mir` twins, their 33
> import rows, `C_RET_RETITEM`, the interpreter aggregate branch, the
> transpiler rename, the `GUARD_ERROR_RI*` macros and the four shims are
> deleted. `Function.returns_ret_item` is a reserved bit at its old position
> because generated code bakes in the shift of `mir_public_return_shape`.
> `pn_select` (async-lowered, no registry entry point) gained a direct import
> row. Gates: 14 error/input/output scripts match goldens; concurrency gtest
> 18/19 with the one failure (`ModuleStateSlabsArePrivateToEachEvalContext`)
> and the hanging `SharedChartAndPdfModulesStayStableAcrossEvalThreads` both
> reproduced on pristine HEAD via stash (pre-existing, EvalContext territory;
> re-check after item 3).

### 5.1 What the rulings already say

D1.4v3: a fallible native function returns one merged `Item`, its success
value or an ERROR-tagged Item carrying the `LambdaError*`. D6.4.1: public
sys funcs return boxed `Item` at the runtime/JIT boundary. D5.2.3: a C
helper or sys func owns no watermark, so it needs no return shape at all.

### 5.2 What the tree actually does

`RetItem { Item value; LambdaError* err; }` (`lambda.h:2116`, spelled again
in `lambda.hpp`) is a 16-byte aggregate that MIR cannot return. So:

- About fifty functions return it: `lambda-proc.cpp` 18, `concurrency`
  8 (+7 declarations), `http_module` 6 and its stub 6, `lambda-eval` 4,
  `lambda-data-runtime` 1.
- 41 `extern "C" Item <name>_mir(...)` twins exist only to call the real
  function and fold the pair back into one Item (`ri_to_item`).
- 34 registry rows carry `C_RET_RETITEM` (`sys_func_registry.c:554` and
  on); the transpiler rewrites the callee name to the `_mir` twin at
  `transpile-mir.cpp:18429`; the interpreter has its own aggregate-call
  branch at `interp.cpp:1043`, whose comment explains that calling a
  RetItem function through an Item prototype "drops `.err` silently".
- `Function.returns_ret_item` is a flag bit with two uses.
- Shims `ri_ok`, `ri_err`, `item_to_ri`, `ri_to_item` exist twice (C and
  C++ headers).

`ri_err(e)` always sets `value = ITEM_ERROR`; `err2it(e)` produces the
tagged Item every caller ends up with. There is no information in the
aggregate that the merged Item does not already carry. The one subtlety —
`item_to_ri` maps a pointer-less `ITEM_ERROR` to `err = (LambdaError*)1`
so `.err` is non-null — is a boolean callers already get from
`item_is_error()`.

### 5.3 Rulings

**SCU17 — No C aggregate return exists at any Lambda boundary.** The
retirement is one coherent change, not a per-function drip:

1. Every `RetItem f(...)` becomes `Item f(...)`; `ri_ok(v)` → `v`,
   `ri_err(e)` → `err2it(e)`. Callers inside the runtime that read `.err`
   switch to `it2err(item)`.
2. The 34 registry rows become `C_RET_ITEM` with `c_func_name` pointing at
   the real function; the 41 `_mir` twins are deleted.
3. `C_RET_RETITEM`, the interpreter branch, the transpiler rename,
   `returns_ret_item` (its bit joins `reserved_flags`; the union width is
   unchanged so `Function` layout is untouched) and the four shims in both
   headers are deleted in the same commit.

The concurrency family (`pn_send`, `pn_receive`, `pn_wait*`, `pn_select`,
`pn_sleep`, `pn_io_read`) is the only group whose callers are also
runtime-internal; they are converted first and their unit tests run
standalone before the registry rows flip.

### 5.4 Acceptance

- Grep gate: `RetItem`, `C_RET_RETITEM`, `_mir(`, `ri_ok`, `ri_err`,
  `item_to_ri`, `ri_to_item`, `returns_ret_item` all 0 hits.
- Error-propagation suite (`test/lambda/error*`), sys-function dispatch,
  interpreter/JIT parity (`make interp-sweep`), wide-scalar return tests,
  concurrency and HTTP module tests, all green.
- The parent's quantitative gate "typed/boxed `Ret*` structs = 0" is met.

---

## 6. Dependencies, ordering and gates

```text
item 4 (parser)   ─┐
item 5 (RetItem)  ─┤  independent; land first, any order
                   │
item 1 (resolver) ─┤  no spec change; touches shape/GC/MIR — own branch
item 2 (ABI)      ─┤  no spec change; touches transpile-mir — own branch,
                   │  rebase onto the current D8.1.1 satellite head
item 3 (context)  ─┘  no spec change; touches JS/Radiant — own branch
```

Item 2 does not depend on item 1, but both edit `transpile-mir.cpp` and
should not be in flight at once. Item 3 conflicts with neither.

Every item's gate includes `make build`, `make test-lambda-baseline` at
100 %, and the item-specific checks above. Items 1 and 2 additionally run
the AWFY subset on a **release** build against the archived control
(rule 10; one run at a time; verify the 21.7 MB binary was not clobbered
by a test build). Items 1–3 each end with a struct census re-run
(`python3 utils/struct_census.py --top 35`) and the numbers recorded in
the parent proposal's §2 table.

Because rule 17 requires it: no item here changes an `S#`/`D#` ruling.
The first change that *would* is SCUO1, and it is a question for the user
before any code moves.

---

## 7. Open issues

- **SCUO1 — Contiguous `Shape`/`ShapeField[]` and descriptor-aware
  identity.** Needs D3.4.1→v2 (replace the chain with the immutable
  array) and D3.4.2→v2 (identity includes the resolved descriptor, not
  only `TypeId`). Ask before starting. SCU9 is designed so that the stored
  descriptor migrates unchanged into `ShapeField`.
- **SCUO2 — Compact vs. full `LaneStorageDesc`.** The current record is
  24 bytes (two `Type*` plus 8 bytes). Stored per `ShapeEntry` that is
  acceptable; stored per `ShapeField` in a contiguous shape it is not.
  Decide the compact 8-byte form (`kind, width, nullable, flags,
  value_domain`) when SCUO1 opens; `semantic_contract` is then
  `ShapeField::contract`, already adjacent.
- **SCUO3 — `EvalContext` composition over inheritance.** Cosmetic today;
  revisit only if a second frozen-prefix consumer appears.
- **SCUO4 — `MirTranspiler`/`VarEntry`/`NameEntry` ratchet.** From
  2026-09-07, no new fact field without a named canonical owner. The
  split itself (parent §9.1/§9.3) waits for the D8.1.1 tuning to settle.
- **SCUO5 — `TypeType` deletion.** Pull forward as its own small change
  after item 1, because the resolver's `LMD_TYPE_TYPE` unwrap
  (`contract_unwrap_type`) is exactly the code that disappears with it.

---

## Appendix A — Implementation notes (brief)

- **Item 1 resolver signature.** Keep `bool
  lambda_type_lane_storage_desc(Type*, LaneStorageDesc*)` during
  migration but make it return `true` for every well-formed input;
  convert the fifteen existing callers that branch on `false` to test
  `desc.kind == LANE_STORAGE_ITEM` instead. Then rename to
  `lane_storage_desc_for(Type*)` returning by value.
- **Item 1 storage on `ShapeEntry`.** Fill in the three constructors
  (`build_ast` literal shapes, `shape_pool` interning, runtime transition
  rebuild) and assert `entry->storage.kind != LANE_STORAGE_INVALID` in
  `map_shape_field_to_item` under debug.
- **Item 2 producer.** `fn_return_analysis_compute()` returns the
  contract; `MirFunctionPlan` construction takes `const FnAbiContract*`;
  `sys_func_registry_init()` allocates one contract per row from a
  static table. The `em_companion_transport()` helper becomes a read of
  `abi->companion`.
- **Item 3 accessors.** Add to `runtime-state.h`, mark `LAMBDA_DEPRECATED`
  once callers are converted, delete with the field.
- **Item 4 allocators.** Use `lib/arraylist.h` and `lib/hashmap.h` backed
  by the `Input` pool (`InputAllocationContext`), not `memtrack`, so
  teardown is owner-wide (D4.1.4v4).
- **Item 5 order of commits.** (a) concurrency family + its tests; (b)
  everything else + registry rows + twin deletion + dispatch deletion;
  (c) header/shim deletion. (b) and (c) may be one commit.

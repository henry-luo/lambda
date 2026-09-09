# Lambda Design: LambdaJS Struct Authority — The Four That Matter

> **Status: DRAFT for user ratification — 2026-09-07.** This is the design
> record for the highest-yield items of
> [`Lambda_Proposal_JS_Struct_Clean_Up.md`](Lambda_Proposal_JS_Struct_Clean_Up.md)
> (the parent proposal; its census and rules JSCU1–JSCU8 are assumed), read
> against the tree **after** the five Lambda-side items of
> [`Lambda_Design_Structs.md`](Lambda_Design_Structs.md) landed (SCU7–SCU17,
> 2026-09-07). It extends the parent's `JSCU` ledger with **JSCU9–JSCU26**
> and opens **JSCUO3–JSCUO7**. JSCUO1 was ratified on 2026-09-07 as
> **JSCU25** and JSCUO2 as **JSCU26**; JSCU26 is the one item that revised
> a formal ruling (D5.1.1→v2, spec 1.51.0, same day, rule 17). Every other
> item *implements* a ruling that already stands.
>
> **The framing change.** The parent proposal is written as a LambdaJS-local
> clean-up. Re-reading it against the tree shows that three of its four
> structural patterns already have a Lambda-side twin: the runtime has its
> own growable rooted vector (`LambdaAsyncFrame`), its own lazy `void*`
> capsules on `EvalContext`, and its own `Function` record that mixes code
> facts with value state exactly as `JsFunction` does. Every item below is
> therefore designed as **one mechanism owned by the runtime and consumed by
> LambdaJS**, not as a second JS-private mechanism (D1.3: guests reuse
> contracts, not accidents).
>
> **Spec linkage.** Item 1 implements **D5.1.1v2**, **D5.1.3**, **D4.3.3**
> and **D6.3.1**; item 2 implements **D5.1.1v2** and **D5.4.2**; item 3 implements
> **D5.4.2**, **D5.4.3** and **D5.4.4**; item 4 implements **D6.2.1**,
> **D6.2.2v2** and **D5.4.1**; the two banked fixes implement **D1.9** and
> **D7.4.1v2**. Guardrails that must survive every item: **JSCU4** (precise
> roots stay precise and address-stable), **JSCU8** (semantic distinctions
> are not collapsed; the parent's §11 table stands), **D5.4.4** (no lock,
> atomic or catalog lookup on a repeated path).
>
> **Scope.** `lambda/js`, the realm-neutral DOM state in `lambda/dom`, the
> Node session in `lambda/jube` and `lambda/module/node_core`, plus the
> runtime primitives in `lambda/runtime` that the items promote. Radiant is
> a compatibility gate. All anchors were re-resolved against `master` on
> 2026-09-07.
>
> **Phase 2 addendum status: RATIFIED by user — 2026-09-09; implementation in
> progress.**
> Section 8 re-measures the post-JSCU9–JSCU24 tree and extends this ledger with
> **JSCU27–JSCU35**. The addendum changes no formal ruling: it continues the
> implementation of **D1.2–D1.3**, **D2.4.1**, **D5.1.1v2**, **D5.1.3**,
> **D5.3.5**, **D5.4.2–D5.4.4**, **D6.2.1–D6.2.3v2**, **D6.3.1**,
> **D7.4.1v2–D7.4.3**, **D8.1.3v10** and **D8.2.4–D8.2.6**. Its controlling
> rule is stricter than merely reducing `sizeof`: **one semantic concept has
> one authority; one storage/lifecycle mechanism has one runtime data
> structure.**

## 0. Why these four, and why in this order

The parent proposal's §2 table is three weeks old. Re-measured today with
the debug build configuration:

| Structure | Parent §2 | 2026-09-07 | Movement |
|---|---:|---:|---|
| `JsRuntimeState` | 1,138,728 B | **1,075,368 B** | trace/permission/crypto moved to the lazy Node session |
| of which `generators[4096]` | ~360 KB | **819,200 B** | record grew to 200 B; now **76 %** of the struct |
| of which `async_contexts[256]` | — | 32,768 B | unchanged |
| `JsFunction` | 280 B / 41 fields | **328 B** | AST-body fields added; moving *away* from the target |
| `JsFuncCollected` | 984 B / 58 fields | **96 B** | facts moved to the shared `FnAnalysis`; parent §9.1 is effectively done |
| `JsMirTranspiler` | 23,560 B | 30,840 B | grew |
| `Function` (Lambda) | — | 72 B | the record `JsFunction` should share a header with |

Three parent items are already done or mostly done and are **not** here:
the Node-only slabs are lazily owned by `NodeRuntimeSession`
(`lambda/jube/jube_registry.cpp:167`); the assert and deep-equal pair stacks
share `js_object_pair_traversal.hpp` (parent §7.7); and the compiler
pre-pass record consumes the shared `FnAnalysis` and `AstFunctionId`
(parent §9.1).

| # | Item | Parent stage | Why it is first-tier | Formal ruling it implements |
|---|------|--------------|----------------------|-----------------------------|
| 1 | Suspension state owns itself: generators and async frames become GC-owned carriers; a JS activation is a task; the Lambda frame joins the same carrier | P3, §6.1–6.2 | 76 % of the context struct, a slot-recycling identity hazard, two hard functional caps, and 2,049 permanent roots registered before any script runs | D5.1.1v2, D5.1.3, D4.3.3, D6.3.1 |
| 2 | One rooted vector in the runtime; JS root ranges, reset registry and per-range epochs are deleted | P1, §5 | 19 rooted-state subclasses, 44 epoch fields, 62 direct root registrations, a 64-entry reset registry with a hand-coded exemption; the runtime already has a third private copy of the same shape | D5.1.1v2, D5.4.2 |
| 3 | One capsule table on `EvalContext`; `JsRuntimeState` becomes a directory; DOM state becomes a peer, not a child, of JS | P2, §4 | Seventeen ad-hoc `void*` capsules across `EvalContext` and `JsRuntimeState`, three copies of the ensure-function shape, two manual lifecycle fan-outs; DOM state that Lambda `import dom` also drives is reachable only through the JS capsule | D5.4.2, D5.4.3, D5.4.4 |
| 4 | One `FunctionCode` / value split shared by `Function` and `JsFunction` | P6, §8 + Lambda parent §6.3 | Two 4-byte-magic-distinguished layouts under one `TypeId`, two tracers, two `runtime_context` fields that SCU14/15 just made removable | D6.2.1, D6.2.2v2, D5.4.1 |
| 5 | Two banked fixes: `ParsedRequest` becomes spans; crypto handles use the Jube rid table | P5, §7.1, §7.4 | An 815 KB automatic variable per HTTP request; four 4,096-pointer tables next to a rid table that already exists | D1.9, D7.4.1v2 |

Recommended landing order is **5, 1, 2, 3, 4**: the two cheap isolated
fixes first so they are banked; then the capacity-and-hazard killer, which
depends on nothing; then the root primitive, which item 3's lifecycle
contract needs; then the capsule table; and last the callable split, which
is the riskiest because generated code reads pinned offsets.

What is **not** here and why:

- Web object state (parent P4: observers 1.6 MB, DOM collections 754 KB,
  XHR 73 KB). All three are allocated on first use, so a non-DOM workload
  pays nothing today. Item 3 turns them into capsules; removing their caps
  is a follow-on. JSCUO5.
- The compiler emitter split (parent P7, §9.2–9.3). `JsFuncCollected` is
  done; `JsMirTranspiler` is per-compilation transient state, and the
  Lambda parent's SCUO4 parks the twin `MirTranspiler` split until the
  D8.1.1 tuning settles. The same ratchet applies here from today: **no new
  fact field on `JsMirTranspiler` without naming its canonical owner in the
  commit**. The one concrete duplicate — three 512-entry closure snapshot
  forms — is JSCUO4.
- Catalog relations and the residual `JS_*_MAX` audit (parent P8) and the
  doc close-out (P9). They follow the four items; they do not lead them.

---

## 1. Suspension state owns itself

### 1.1 What the rulings already say

D5.1.1: three stack-like mechanisms with distinct owners; heap async frames
are an Item region plus a never-scanned tail; **non-LIFO lifetimes own
their scalars in tail regions**. D5.1.3: **suspension is a re-homing
barrier** — an async frame is a tail-bearing heap container. D4.3.3: VMap
traces and finalizes through allocation-free vtable callbacks. Parent JSCU1:
a generator's suspended environment belongs to the generator object, not to
the context that created it. Parent §5.1 names `JsPromise` as the positive
example: a GC-owned VMap that owns its result and reactions with no
context-wide capacity.

### 1.2 What the tree actually does

- `JsRuntimeState::generators[JS_MAX_GENERATORS]`
  (`lambda/js/js_runtime_state.hpp:953`) holds every
  `JsGeneratorStateRecord` (`:682`, 200 B): 4,096 × 200 B = 819,200 B,
  embedded by value, paid by every context.
- The visible generator object is `JsGeneratorMapCarrier { Map base;
  int64_t generator_index; }` (`lambda/js/js_runtime.cpp:96`). Its tracer
  (`:27780`) indexes the context table to mark the environment, class home,
  delegate and AST records. The state's identity is therefore an integer
  into a recyclable pool, not the object.
- `js_generator_create_current` (`:27927`): at the cap it **reuses the
  first slot whose `done` flag is set**. A completed generator object
  retained by script still names that slot; the next `next()` on it reads
  another generator's state. This is the parent §6.1 identity hazard, and
  it is live code, not a theoretical one.
- `JsRuntimeState::async_contexts[JS_MAX_ASYNC_CONTEXTS]` (`:955`, 256 ×
  128 B). `js_async_register_roots_once` (`js_runtime.cpp:31280`)
  registers **eight roots per slot plus one**, 2,049 individual
  `heap_register_gc_root` calls per heap epoch, whether or not the script
  contains an `async` function. `js_async_context_create_current`
  (`:31447`) refuses the 257th live activation (`:31456`); the resume and
  reject handlers (`:31425`, `:31434`) receive the **slot index as an
  Item**, so a completed slot can be reused under a still-pending promise
  reaction.
- Both records carry a `Context* runtime_context` field, which D5.4.1's
  canonical context made redundant on 2026-09-07 (SCU14/SCU15).
- The environment is already the right carrier. `js_env_rehome_scalars`
  (`lambda/js/js_runtime_function.cpp:1066`) re-homes side-number scalars
  into a `GC_TYPE_JS_ENV` object whose first half is Items and second half
  is an unscanned raw tail (`lambda/runtime/gc/gc_heap.c:1676`). That is
  D5.1.3's "tail-bearing heap container" made literal. Only the **owner**
  of that environment is wrong: a table slot instead of the object.
- The runtime's own suspension frame, `LambdaAsyncFrame`
  (`lambda/runtime/concurrency.cpp:80`), is a task-owned native record with
  `slots` + `slot_is_item` registered as a root range. It is the third
  suspension carrier for one ruling; item 1 does not touch it (JSCUO2 asks
  whether it should converge).

### 1.3 Rulings

> **Status: JSCU9 LANDED 2026-09-08.** The generator table
> (`generators[4096]`, `generator_count`) and `JS_MAX_GENERATORS` are gone;
> `JsGeneratorMapCarrier` now embeds the `JsGeneratorStateRecord` inline
> (`{ Map base; JsGeneratorStateRecord state; }`, 232 B, in the 256 B object
> class). The tracer and `js_get_generator` read `&carrier->state`; a new
> `js_generator_map_heap_destroy` (called from `gc_finalize_js_native_map`)
> frees a collected generator's interpreter continuations, so the batch-reset
> and context-teardown table walks are deleted. `runtime_context` stays inside
> the inline state: the resume guard `js_mir_owner_is_current(gen->runtime_context)`
> reduces to `context == owner`, so it must compare a captured owner against
> the current TLS to catch a wrong-thread resume — passing the ambient context
> would defeat it. Fully dropping it is the SCU14 generalization, deferred.
> Result: `sizeof(JsRuntimeState)` **1,075,368 → 256,160 B** (−819,208, the
> table). Gates: test262 baseline **0 regressions** (35046/35046, 40261
> baseline passing); JS gtest 361/361; MIR GC stress 107/107; 5,000 live
> generators with retained-completed identity, and delegation/completion under
> `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, all correct.
> JSCU10/JSCU25/JSCU26 (async frame lifetime via `LambdaTask`) are the next
> sub-unit; they interlock (a GC-owned async frame's survival between create,
> synchronous drive, and first suspend is exactly what the task's registration
> provides), so they land together rather than split.

### 1.3 Rulings (original)

**JSCU9 — Generator state is carried by the generator object.** The
carrier becomes `struct JsGeneratorCarrier { Map base; JsGeneratorState
state; }`, the same inline-payload form `JsProxyMapCarrier` already uses
(`js_runtime.cpp:91`). `JsGeneratorState` is the current record minus
`type_id`, `runtime_context` and any index; it keeps the state function,
the re-homed `GC_TYPE_JS_ENV` environment, state number, execution flags,
class home, delegation state and the AST-interpreter records. The existing
Map-carrier trace hook (`js_native_trace`, installed at
`lambda/runtime/lambda-mem.cpp:420`) marks the carrier's own fields; the
tracer at `js_runtime.cpp:27780` stops indexing a table. There is no
context table, count, cap or recycling rule. `JS_MAX_GENERATORS` has no
definition after this item.

>
> **Status: JSCU10 LANDED 2026-09-08.** The async table (`async_contexts[256]`,
> `async_context_count`) and `JS_MAX_ASYNC_CONTEXTS` are gone. A suspended
> activation is a GC-owned `JsAsyncFrameCarrier { Map base;
> JsAsyncContextStateRecord state; }` (map_kind `MAP_KIND_ASYNC_FRAME`,
> `type=NULL`, so `js_object_meta` classifies it `JS_CLASS_NONE` and no
> class-based sub-trace touches it). `js_async_frame_map_gc_trace` marks the
> frame's own edges and now also its for-await-of loop continuations, which the
> old permanent-root table never traced (a latent precision gap closed here).
> Create/start/drive/resume/reject/get_promise thread the **frame Item**, not an
> index; the resume/reject reactions bind the frame Item so the awaited promise
> retains it. The UAF window the design worried about is closed natively —
> `create` roots the frame while allocating the promise, `drive` roots it across
> the synchronous body, and the generated wrapper's adjacent create→start→
> get_promise calls have no GC between them — so **JSCU10 stands correct without
> JSCU25**. `js_async_register_roots_once` (2,049 registrations) shrinks to a
> single epoch-guarded scratch root for the await handoff value. Result:
> `sizeof(JsRuntimeState)` **256,160 → 223,384 B** (−32,776); combined with
> JSCU9, **1,075,368 → 223,384 B**. Gates: JS gtest 361/361; MIR GC stress
> 107/107; 1,000 simultaneous activations (old cap 256) with correct results;
> multi-await chains, rejection, and for-await-of correct under
> `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
>
> **JSCU25/JSCU26 reassessed:** the sole coupling rationale in §6 was the UAF
> window, which JSCU10 solved natively. JSCU25 (async activation as a weakly
> registered `LambdaTask`) and JSCU26 (`LambdaAsyncFrame` onto the GC carrier)
> therefore deliver no further memory or correctness win — they are pure
> architectural unification toward D6.3.1's one-scheduler model, touching the
> scheduler's weak registration and the microtask/task ordering. They are best
> done as a dedicated focused change rather than bundled with the table
> removal; recorded for a follow-up rather than forced in here.

**JSCU10 — An async activation is a GC-owned `JsAsyncFrame`.** One
carrier per suspended activation, allocated on first suspension, owning the
compiled artifact reference, the re-homed environment, state number, result
Promise, `this`, module-state id and the AST-interpreter records. The
resume and reject handlers receive the **frame Item**, not an index; the
Promise reaction retains the frame, the frame retains the Promise, and the
cycle is collected when neither is reachable, which is the ordinary
`JsPromise` contract. `js_async_register_roots_once` and the 2,049 roots are
deleted. `JS_MAX_ASYNC_CONTEXTS` has no definition after this item.

**JSCU11 — One suspension environment carrier for LambdaJS.** MIR
generators, MIR async functions and their AST-interpreter forms all suspend
into `GC_TYPE_JS_ENV` (Items first, raw tail second, `js_env_rehome_scalars`
at the barrier). Promise, generator and async frame remain **three state
machines** (parent §11); what they share is the environment carrier, the
Map/VMap ownership form, and precise self-tracing — not one universal
resumable record.

**JSCU25 — A JS async activation is a `LambdaTask` (ratified
2026-09-07).** D6.3.1's "one scheduler per context" is taken literally:
the `JsAsyncFrame` carrier of JSCU10 is the `frame` of a `LambdaTask`
created at first suspension, with a resume entry that drives the JS state
machine one step and a destroy entry that is the carrier's finalize. The
task gives the activation what every task has — registry membership,
scope parenting, cancellation, observers and the completion bridge that
`lambda/runtime/concurrency_js.cpp` already implements for Lambda handles
awaited from JS — and nothing is invented beside it. Four constraints keep
the ruling inside D6.3.1 and D5.4.4:

1. **Readiness stays with the microtask queue.** An `await` continuation
   is a PromiseReactionJob; that job resumes its task directly inside the
   microtask checkpoint. The scheduler's FIFO run queue resumes Lambda
   tasks as macrotasks and never resumes a JS frame on its own, so a
   Lambda resume still cannot interrupt a JS job or its checkpoint.
2. **The Promise is the handle.** `lambda_task_create` today `heap_calloc`s
   a VMap handle and registers three roots and a mailbox range per task
   (`concurrency.cpp:737–774`). A JS activation task takes the carrier
   Item as its handle, allocates **no** mailbox until a send or receive
   asks for one, and registers no root range: the carrier traces the
   environment, Promise, `this` and AST records itself (JSCU10).
   Making the mailbox lazy for every task is the general fix and is part
   of this item.
3. **Liveness is script reachability, not registry membership.** A Lambda
   `start` task is retained by the scheduler until it is done; a JS
   activation whose awaited promise can never settle is garbage, as in
   every other engine. The scheduler therefore holds JS activation tasks
   **weakly**: the carrier is retained by the pending reaction, and the
   carrier's finalize detaches the task (allocation-free, D4.3.3). This is
   a semantic distinction kept on purpose (JSCU8), not a leak to fix later.
4. **Generators are not tasks.** A generator resumes synchronously under
   its caller's `next()`; it has no readiness and no completion. JSCU9
   stands as written; only async functions and async generators' awaiting
   halves become tasks.

**JSCU26 — `LambdaAsyncFrame` converges onto the suspension env carrier
(ratified 2026-09-07; D5.1.1→v2).** The two frames were the same shape
inside and differed only in owner and lifetime rule:

| | `LambdaAsyncFrame` (`concurrency.cpp:80`) | `GC_TYPE_JS_ENV` (`gc_heap.c:1676`) |
|---|---|---|
| storage | `mem_calloc` array, `slots[0..cap)` Items, `slots[cap..2cap)` raw spills | one GC object, first half Items, second half raw tail |
| how the collector sees it | task registers the Item half as a root range; re-registers on every growth (`:1120–1150`) | traced through whoever points at it; never registered |
| what keeps it alive | the task, until `destroy_frame` | reachability (generator object, async frame, closure) |
| how generated code reaches slots | twelve `lambda_async_frame_*` helpers, no direct offsets | `Item* env` parameter |

Ruling: `lambda_async_frame_enter_current` allocates the GC carrier
(the `GC_TYPE_JS_ENV` form under a language-neutral name) instead of a
native array; the task retains it through the one rooted slot it already
has, so `frame_roots` and the per-frame range registration go away;
growth is allocate-larger-and-copy with one pointer store into
`task->async_cursor`, which happens only in the running task at frame
entry; the twelve helpers keep their signatures, so no generated Lambda
MIR changes. A `start` task keeps **strong** registry retention so a
parked task's frame is never collected under it; the weak mode is
JS-only (JSCU25 §3). Item 2 (a) is subsumed: there is no native Item lane
left in a frame to migrate. D5.1.1 v1 said the Item region is
"root-registered", which described the native form only; v2 says the
region is traced through its owner, the task or the suspended object.
Lands after JSCU25, with the forced-GC gates of §1.5 plus the Lambda
async goldens in both tiers.

### 1.4 What does not change

- The observable generator object stays a `Map` carrier with the ordinary
  prototype chain and property face; only its payload moves inline.
- `JsPromise` is untouched; it is the model, not a target.
- `LambdaTask` gains a lazy mailbox and a weak-registration mode; its
  scheduler, scope, mailbox and wait-group semantics are untouched.
- `LambdaAsyncFrame`'s helper API, write-through model and the SF20
  suspension-barrier invariant are untouched; only its storage and owner
  edge change (JSCU26).
- The AST-interpreter continuation records
  (`JsInterpGeneratorLoopContinuation` and friends) keep their current
  ownership; they move with the record they are fields of.

### 1.5 Acceptance

- Grep gate: `generators[`, `async_contexts[`, `generator_index`,
  `JS_MAX_GENERATORS`, `JS_MAX_ASYNC_CONTEXTS` each 0 hits.
- `sizeof(JsRuntimeState)` drops by at least 851,968 B (the two tables).
- Forced-GC stress (`make test-mir-gc-stress`, `make test-gc-rooting`) with
  a new case at every generator and async suspend/resume edge under
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_POISON_FREED=1`.
- New targeted cases: a completed generator object retained while 5,000
  new generators are created and driven, then resumed, must throw the
  spec's "generator is already running/completed" result and never another
  generator's value; 1,000 simultaneous pending `async` activations resolve
  in order; a rejected activation after 300 others releases its frame.
- JSCU25 cases: a JS `async` function awaiting a Lambda `start` handle and
  a Lambda `pn` awaiting a JS Promise, each under forced GC; the
  scheduler's live count reflects JS activations while pending and drops
  when their carriers are collected; an activation parked on a
  never-settling promise is collected after its last reference dies; the
  concurrency suite (`RuntimeGlobalsConcurrency` at its two pre-existing
  failures, no new ones).
- JSCU26 cases: the Lambda async goldens (`test/lambda` `start`/`wait`
  fixtures) in both tiers under `LAMBDA_GC_FORCE_EVERY=1
  LAMBDA_POISON_FREED=1`; a frame that grows across three suspensions
  keeps every slot; a task parked on a mailbox nobody sends to is still
  live after ten forced collections (strong retention); the collector's
  registered-range count no longer scales with live tasks.
- `make test-js262-prelim`, `make test262-baseline`, `make node-baseline`
  at their recorded baselines; `make test-lambda-baseline` 100 %.
- Release-build Test262 batch throughput and a Promise/microtask
  micro-benchmark within noise of the archived control; the lazy mailbox
  and the GC-allocated frame must not move the `start`-heavy concurrency
  benchmarks.

---

## 2. One rooted vector in the runtime

### 2.1 What the rulings already say

D5.1.1: the root side stack is precisely scanned `[base, top)` for LIFO
lifetimes; non-LIFO state owns its storage. D5.4.2: capsules never
reallocate, because rooted-Item addresses must be stable; **root
registration takes an explicit heap/context and happens at construction,
never in the repeated store**. Parent JSCU2: consumers use one owner and
API and never reproduce its capacity, root-registration, epoch, counter and
cleanup machinery. Parent JSCU4: never a reallocating buffer whose Items
move.

### 2.2 What the tree actually does

The runtime already offers everything except the vector:

- LIFO: `RootFrame` / `Rooted<T>` / `RootSpan`
  (`lambda/runtime/lambda-root-frame.hpp`) over the side stack.
- Ranges: `heap_register_gc_root_range` / `heap_unregister_gc_root_range`
  and their explicit-context forms (`lambda/runtime/heap_api.h:29–40`); the
  collector's range registry is itself dynamic (`gc_heap.c:481`).
- A growable rooted vector, **privately**: `LambdaAsyncFrame` growth
  (`concurrency.cpp:1120–1150`) unregisters the old block, allocates,
  copies through `owned_item_slot_store`, re-registers. Correct, and the
  shape LambdaJS re-implements nineteen times.

LambdaJS instead has:

- `JsRootRange` (`js_runtime_state.hpp:134`, 56 B): fixed `Item slots[N]`
  plus `roots_epoch`, a name, a reset callback and a registered flag.
  `JsRootedState` (`:147`) embeds one; **19** subclasses derive from it.
- **44** `roots_epoch` fields across `lambda/js`, `lambda/dom` and
  `lambda/module`, each comparing itself against `js_heap_epoch`
  (`js_runtime_state.hpp:1024`) on first use.
- **62** direct `heap_register_gc_root(` calls (48 in `lambda/js`, 7 in
  `lambda/dom`, 7 in `lambda/module`), the observer matrix alone
  registering 128 at once (`lambda/dom/dom_observers.cpp:100`).
- **40** `JS_*_MAX` / `JS_*_CAP` / `JS_*_CAPACITY` definitions across the
  three trees, most of them sizing an Item array that lives in the context.
- `root_range_registry[JS_ROOT_RANGE_REGISTRY_MAX]` (`:999`, 64 entries),
  filled by `js_root_range_register_reset` (`js_runtime_state.cpp:342`),
  which logs an overflow and returns `false` at entry 65.
- `JS_RUNTIME_ROOT_STORAGE` (`js_runtime_state.cpp:290`), a thirty-line
  macro catalog binding every fixed range to its slots, run by
  `js_runtime_state_prepare_root_ranges` — a second registry of the same
  ranges.
- `js_root_range_reset_all` (`:383`) walks the registry, clears every
  range, and **special-cases `builtin_cache.roots`** so a preamble reset
  does not lose realm identity anchors. The exemption is the symptom the
  parent's §4.3 describes: reset is a growing checklist with a named hole,
  not a lifecycle.

### 2.3 Rulings

> **Status: JSCU12 LANDED, JSCU14(b) LANDED 2026-09-08.** The primitive is
> `lambda/runtime/root_vector.{h,cpp}` (C-callable, in `lambda-rt`): a POD
> index over fixed 64-slot blocks, each registered through
> `heap_register_gc_root_range_for` before its first Item is published, never
> moved, vacated slots zeroed, retained until destroy. The heap now owns its
> epoch: `Heap::generation` is assigned once in `heap_init` from a process
> monotonic counter and read through `heap_generation_for(Context*)`; a vector
> compares that, not a private copy. Two operations were separated after the
> rooting gate's NO_GC audit caught `js_with_restore_depth → shrink →
> register → mem_realloc`: every non-publishing operation (`pop`, `at`,
> `count`, `clear`, `shrink`) only *observes* a heap replacement (drops the
> dead-heap Items, forgets the registration, allocation-free), and only
> `push` re-registers the retained blocks. Unit test:
> `GCHeapTest.RootVectorGrowsAddressStableAndFollowsHeapReplacement`.
> Migrated (b): the four `JsItemStack`s — with-scope, super-this, domain and
> the Node session's CommonJS module stack — are `RootVector`s; their fixed
> slot arrays, three catalog entries and reset-registry callbacks are gone,
> and their resets are named calls in `js_batch_reset_runtime_caches` on both
> the full and checkpoint paths (super-this and with were already cleared by
> the transient-call and globals resets). The super-this bound-flag array
> stays a parallel POD array with an explicit `JS_SUPER_THIS_STACK_MAX`
> nesting policy (JSCU13). `sizeof(JsRuntimeState)` 223,384 → 221,712 B.
> Gates: `make test-gc-rooting-core` exit 0 (49 NO_GC imports verified,
> 15,795 functions hazard-clean, MIR GC 107/107); GC heap 76/76; JS gtest
> 369/370 (the one failure is the remote `dom_3d_transform_inline_rect`
> fixture, `document is not defined`, pre-existing); Node module/domain slice
> identical to pristine HEAD (30 pre-existing failures, 0 regressed); `with`,
> derived `super` to depth 70, nested `require` and domains correct under
> `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; `make test-gc-rooting-python`
> exit 0; test262 baseline **0 regressions** (40261/40261).
>
> **JSCU14(c) LANDED 2026-09-08.** The eval journals' twelve Item lanes
> (source filenames/code, env keys/old values, global-lexical keys/old
> values, private unscoped/scoped keys, local keys/values, lexical and
> immutable keys) are `RootVector`s, cataloged once by
> `JS_EVAL_STATE_VECTORS` for init/destroy/clear. Each group's binding
> count is its key lane's count; the six `*_count` fields, the twelve
> `JsRootRange`s and their catalog entries are gone. Allocation sites push
> (key lane first, then the value lane, popping the key on failure); frame
> pops shrink to the mark. The pop paths keep the exact old behaviour that
> the entry is *invisible* during its restore (shrink first) while the popped
> key/old value are held in a `RootFrame`, because the old fixed arrays were
> scanned in full beyond the count and so kept them alive implicitly. The
> POD columns (`had_own`, `from_journal`, `immutable`, offsets, frame marks)
> stay parallel fixed arrays under the existing `*_BIND_MAX` checks
> (JSCU13; growth is JSCUO7). `sizeof(JsEvalState)` **41,448 → 4,304 B**;
> `sizeof(JsRuntimeState)` **221,712 → 184,568 B**. Gates: rooting gate exit
> 0 (its eval scripts run under forced GC + poison); JS gtest 369/370; a
> nested-direct-eval probe (journal var write-back, let/const, private names,
> global lexical, indirect eval, 12-deep nested eval, 300-iteration journal
> churn) identical plain and under `LAMBDA_GC_FORCE_EVERY=1
> LAMBDA_GC_POISON_FREED=1`; test262 baseline **0 regressions**
> (40261/40261).
>
> **Residue (census after (c)):** 5 `JsRootRange` fields (with-binding
> cache, event-loop queue/RAF, plus the two on the Node session), 31 catalog
> entries, 42 `js_root_range_ensure_registered` calls, all field-shaped or
> id-keyed (RAF callbacks, global lexical bindings, async hooks, ALS,
> readline, test262 agents). (d)/(e) split two ways: the array-shaped caches (RAF callbacks,
> global lexical bindings, async hooks, ALS, readline, test262 agents) are
> vector-shaped and can follow (c); the ~30 field-shaped ranges (namespace
> and prototype Items registered as a range over struct fields) are not
> vectors at all — they are roots registered at capsule construction, which
> is item 3's lifecycle, so `JsRootRange`, the reset registry and the catalog
> are deleted there, not here.

### 2.3 Rulings (original)

**JSCU12 — `RootVector` is a runtime primitive.** One record in
`lambda/runtime` (beside `lambda-root-frame.hpp`), C-callable:

```c
typedef struct RootVector RootVector;
bool     root_vector_init(RootVector* v, Context* owner, const char* name);
bool     root_vector_push(RootVector* v, Item value);          // MAY_ALLOC (block)
void     root_vector_pop(RootVector* v);                       // clears the vacated slot
Item*    root_vector_at(RootVector* v, int64_t index);         // stable address
int64_t  root_vector_count(const RootVector* v);
void     root_vector_clear(RootVector* v);                     // keeps blocks
void     root_vector_destroy(RootVector* v);                   // unregisters, frees
int64_t  root_vector_high_water(const RootVector* v);          // diagnostic
```

Storage is a chain of **fixed-address blocks** (64 Items each; the first
block may be inline in the owner). A block is registered with
`heap_register_gc_root_range_for(owner, …)` **before** its first Item is
published and is never moved, reallocated or freed while its heap can scan
it; `destroy` unregisters through `heap_unregister_gc_root_range_for`.
Vacated slots are cleared to `ItemNull` so a fully scanned block never
retains a dead Item. There is no workload-visible maximum.

**JSCU13 — Items and POD are never interleaved in a scanned region.** A
consumer whose logical entry is several Items plus metadata either keeps
parallel `RootVector`s for the Item lanes and an ordinary `ArrayList` for
the POD, or moves the entry to a GC-owned carrier (item 1's form). The
collector never sees a pointer, integer or flag in a scanned slot.

**JSCU14 — Consumers, in migration order.** (a) Subsumed by JSCU26: once
the frame is a GC carrier there is no native Item lane to migrate; if
item 2 lands before JSCU26, skip (a) rather than migrate storage that is
about to be deleted. (b) The JS LIFO item stacks (`JsItemStack`: with-scope,
super-this, domain stack). (c) The eval source/binding journals
(`JsEvalState`, `JsEvalBridgeState`, `JsEvalLocalState`, 41 KB of fixed
lanes). (d) Realm caches: builtin cache, function cache keys/values, global
lexical bindings, readline maps, async hooks, ALS instances, diagnostics
channels, RAF/timer/mock-wait handles. (e) DOM observer roots and Node
resource slot values. After (e), `JsRootRange`, `JsRootedState`,
`root_range_registry`, `JS_RUNTIME_ROOT_STORAGE`,
`js_root_range_reset_all` and every `roots_epoch` field have no definition.
The heap epoch is owned once, by the heap the context owns; a capsule
learns about a heap replacement through its lifecycle (item 3), not by
comparing a private epoch on every first use.

### 2.4 What does not change

- `RootFrame` / `Rooted` stay the only LIFO mechanism; a `RootVector` is
  never used for a value whose lifetime is one native activation.
- `heap_register_gc_root` for a **single** long-lived slot embedded in a
  native record stays legal; it is the 128-at-a-time and
  fixed-array-per-subsystem uses that go.
- Semantic caps (with-scope depth 16, Test262 agent count) stay as
  **checked policy** with a defined error; they stop sizing a struct.

### 2.5 Acceptance

- Grep gate: `JsRootRange`, `JsRootedState`, `roots_epoch`,
  `JS_RUNTIME_ROOT_STORAGE`, `root_range_registry` each 0 hits; direct
  `heap_register_gc_root(` in `lambda/js` + `lambda/dom` + `lambda/module`
  below 10, each remaining call on a single embedded slot with a comment
  naming the owner.
- A `RootVector` unit test in `test/test_gc_heap_gtest.cpp`: push past
  three blocks under `LAMBDA_GC_FORCE_EVERY=1`, take `root_vector_at`
  addresses before growth, assert they are unchanged after, assert vacated
  slots read `ItemNull`, destroy and assert the collector's range count
  returns to its starting value.
- `make test-gc-rooting` (all three arms) and `make test-mir-gc-stress`
  green.
- The preamble-reset Test262 batch mode runs its full baseline with the
  `builtin_cache` exemption deleted, which proves the checkpoint contract
  (item 3) rather than the exemption carries realm identity.

---

## 3. One capsule table on `EvalContext`

### 3.1 What the rulings already say

D5.4.1: one canonical `EvalContext` per isolate. D5.4.2: context state
lives in **lazily-allocated opaque capsules**; the JIT-visible prefix is
frozen; capsules never reallocate. D5.4.3: *reset becomes construction* —
reset, replace-heap and destroy are three distinct ordered contracts.
D5.4.4: frozen registries stay global; one event loop per context; no lock
or atomic on a repeated path. Parent JSCU5: reset is lifecycle, not a
clearing checklist.

### 3.2 What the tree actually does

- `EvalContext` (`lambda/lambda-data.hpp:105–140`) already carries four
  capsules by hand: `render_map_state`, `template_state_store`,
  `jube_node_session` (all `void*`) and `js_state`. The first three have
  51 direct readers across `lambda/` and `radiant/`.
- `JsRuntimeState` (`js_runtime_state.hpp:863`) nests **thirteen** more
  `void*` capsules (`:885–893`, `:920–937`): DOM event, observer, XHR,
  history, collection, foreign-document, fetch, canvas, prototype
  snapshot, two regex caches, dynamic-function cache, compile recovery.
  Each has its own ensure function: `js_observer_runtime_state`
  (`lambda/dom/dom_observers.cpp:73`), `js_xhr_runtime_state_ensure`
  (`dom_xhr.cpp:74`) and the `dom_runtime_state_ensure<State>` template
  (`dom.cpp:1990`) are three spellings of the same shape; 36 such
  allocation sites and 42 direct field references exist.
- `NodeRuntimeSession` (`jube_registry.cpp:167`) is a fourth lazy capsule
  form, with its own generation token and lock.
- Lifecycle is manual: `js_runtime_state_init` (`js_runtime_state.cpp:118`)
  `mem_alloc`s the 1 MB record and zeroes it; `release_heap_resources`
  (`:202`) and `destroy_context` (`:228`) are 25- and 41-line fan-outs that
  each new subsystem must remember to edit. The parent's duplicate
  `env_key_roots` clear in `js_eval_state_reset` is the review risk made
  concrete.
- `JsDeferredMirState` (`:172`, 65,544 B) is two parallel 4,096-entry
  arrays of MIR contexts and source buffers — the code store, hidden inside
  realm state and capped.
- **Misplacement.** The DOM collection, observer and XHR state are defined
  in `lambda/dom`, the realm-neutral DOM core that Lambda `import dom` and
  the `radiant.*` templates drive without a JS realm, yet they are reached
  only through `js_runtime_state.dom_*` and require
  `js_active_runtime_state` to exist (`dom_observers.cpp:74`). A Lambda
  page with observers and no script must construct a JS state to hold them.

### 3.3 Rulings

> **Status: JSCU15 LANDED, JSCU17 LANDED, JSCU18 (partial) 2026-09-08.**
> `lambda/runtime/context_capsule.{h,cpp}` is the directory: `EvalContext`
> gains `capsules[]` plus `capsule_ops[]` at its tail (the frozen `Context`
> prefix and the module-state offsets are untouched, D8.1.3v10), reading a
> capsule is one indexed load, and construction is the only cold path.
>
> **Deviation from the design's literal shape, deliberately.** §4.1 sketches
> one static `JsCapsuleOps context_capsule_ops[]` naming every constructor.
> The tree's library split makes that wrong: a central table would force
> every target linking the directory to carry every subsystem's symbols.
> Instead each subsystem owns an immutable file-static `ContextCapsuleOps`
> beside its implementation and passes it to `context_capsule_ensure`, which
> publishes the ops pointer *with* the slot. The registry stays frozen and
> process-global (D5.4.4) and the lifecycle is still a table walk — the
> runtime simply never names a subsystem symbol. This is closer to JSCU15's
> "one owner" intent than the central table was.
>
> **JSCU17 done:** the eight DOM/web capsules (event, observer, XHR, history,
> collection, foreign-document, fetch, canvas) are context capsules with
> `REALM` lifetime, registered under `lambda/dom`'s own id range. Their eight
> `void*` slots leave `JsRuntimeState`, so DOM state is a *peer* of the JS
> capsule rather than a child of it. Each subsystem's `X_destroy_context`
> became a `destroy` op, and the eight calls in the teardown fan-out
> collapsed to one `context_capsule_destroy_all` (JSCU18's shape, for these
> subsystems). `EvalContext` is 600 B.
>
> **JSCU16 (code store) LANDED 2026-09-08.** `JsDeferredMirState`'s two
> parallel 4,096-entry arrays are replaced by a dynamic `JsCodeStore` of
> `JsCompiledArtifact` records, one owner per compiled unit holding its MIR
> context and the source buffer that unit's generated code still reads. The
> `JS_DEFERRED_MIR_MAX` cap is gone: the store grows by doubling, and the
> old overflow path (which silently leaked a context once the table filled)
> now only triggers on a genuine allocation failure, with the same
> leak-rather-than-`MIR_finish` behaviour because JIT function pointers are
> still live. `sizeof(JsRuntimeState)` **184,504 → 118,976 B** (−65,528);
> `sizeof(JsCodeStore)` is 16 B. The store keeps its current lifetime inside
> the JS capsule; promoting it to a `CONTEXT`-lifetime capsule so compiled
> preamble code explicitly survives a realm reset is the follow-on.
>
> **JSCU16 (realm records) LANDED 2026-09-08.** The five remaining large
> embedded records -- global bindings (18,040 B), the event-loop queue
> (16,504), timers (14,400), async hooks (10,336) and global var module
> bindings (8,272) -- are allocated beside `JsRuntimeState` instead of
> inside it. They are created with the realm rather than lazily, because
> every realm uses them immediately and that keeps every use site
> infallible; the ratchet this serves is "optional subsystem records
> embedded by value: 0", not lazy construction for its own sake.
>
> The obstacle named at the end of item 2 is now resolved: each record's
> precise root span is published by `js_runtime_state_alloc_records` at the
> moment the record is allocated, so their five catalog entries are deleted
> outright rather than relocated. Registration stays epoch-guarded through
> the existing `js_root_range_ensure_registered` at the use sites, so heap
> replacement behaves exactly as before. `sizeof(JsRuntimeState)`
> **118,976 → 51,464 B**; the catalog is down to 26 entries from 43 at the
> start of item 2.
>
> Gates: test262 baseline **0 regressions** (40261/40261); rooting gate exit
> 0 (15,809 functions hazard-clean); JS gtest 369/370; the Node
> timers/async_hooks/process slice **72 pass with the same 66 pre-existing
> failures as pristine HEAD, 0 regressed**; nextTick/microtask/timer
> ordering, global lexical bindings and `async_hooks` correct under
> `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
>
> **JSCU16 (second record batch) LANDED 2026-09-08.** Twelve more records
> follow the same shape: readline, builtin cache, global string caches,
> assert, intrinsics, string concat, AsyncLocalStorage, constructors, the
> Test262 agent, process, iterators and namespaces. Their catalog entries
> needed no relocation this time -- allocation happens at realm init, before
> the catalog is ever prepared, so the entries simply follow the pointer.
> `sizeof(JsRuntimeState)` **51,464 → 27,056 B**; 28 records remain embedded.
>
> **A build hazard this exposed, worth stating plainly.** Changing
> `JsRuntimeState`'s layout silently breaks the dynamic Node modules:
> `modules/node-*.dylib` compile against the struct and bake in field
> offsets, and `make build` does not rebuild them. The symptom is not a link
> error but a wrong-offset read -- here, `jube_specifier_resolve("crypto")`
> failing, which surfaced as `tune4_global_callable_binding` losing its
> lazy-global section while `process` and `Buffer` (built into the
> executable) still worked. **Any layout change to `JsRuntimeState` must be
> followed by `make build-node-{core,fs,net,crypto,zlib}` before any gate is
> believed**, and an A/B must rebuild the modules on both sides.
>
> Gates, all with modules rebuilt on both sides: test262 baseline **0
> regressions** (40261/40261); rooting gate exit 0; JS gtest 369/370; the
> Node timers/async_hooks/process/crypto slice **137 pass with the same 133
> pre-existing failures as pristine HEAD, 0 regressed**.
>
> What is *not* yet done: the ensure paths still guard on
> `js_active_runtime_state`, so a Lambda-only page still constructs no DOM
> capsule. Relaxing that guard is JSCUO3 and is a behaviour change, kept out
> of a mechanism swap. The remaining seven JS-side `void*` capsules
> (prototype snapshot, dynamic-function cache, compile recovery, the two
> regex caches, atomics, TLS ticket state) and the ~50 embedded records of
> JSCU16 are the next slices; `sizeof(JsRuntimeState)` is 184,504 B and the
> ≤ 1 KB ratchet belongs to JSCU16.
>
> Gates: test262 baseline **0 regressions** (40261/40261); rooting gate exit
> 0 (15,803 functions hazard-clean); JS gtest 369/370; DOM UI-automation
> suite **116/119 identical to pristine HEAD** (the three failures
> `dom_sortable_drag`, `dom_splide_swipe`, `dom_pkg_listbox` reproduce
> unchanged on HEAD, as does `js_computed_style_table`). The aggregate
> Radiant baseline is order-dependent — two consecutive runs of the same
> binary produced different failure sets — so the per-suite A/B above is the
> attribution, not the aggregate.

### 3.3 Rulings (original)

**JSCU15 — The capsule directory and its ops table live on
`EvalContext`.** At the tail of `EvalContext` (after `execution_depth`,
per D8.1.3v10's "new shell bookkeeping at the tail"):

```c
void* capsules[CONTEXT_CAPSULE_COUNT];
```

with a **frozen, process-global** descriptor table:

```c
typedef struct ContextCapsuleOps {
    const char* name;
    ContextCapsuleLifetime lifetime;      // CONTEXT | REALM | RESOURCE | DIAGNOSTIC
    void* (*construct)(EvalContext* context);
    void  (*release_heap)(EvalContext* context, void* capsule);  // heap replacement
    void  (*destroy)(EvalContext* context, void* capsule);
} ContextCapsuleOps;
extern const ContextCapsuleOps context_capsule_ops[CONTEXT_CAPSULE_COUNT];
```

Capsule IDs are a compile-time enum; `context_capsule(ctx, ID)` is an
inline load with no check, and `context_capsule_ensure(ctx, ID)` is the one
cold construction path. Iteration over the table happens only in the three
lifecycle contracts. This is what D5.4.4 permits: the registry is immutable,
the per-context pointers are ordinary owner-thread loads. The four existing
`EvalContext` fields become IDs; their 51 readers convert mechanically
through the accessor.

**JSCU16 — `JsRuntimeState` is a directory of JS capsules, ≤ 1 KB.** The
embedded-by-value subsystem records become capsules with their own IDs
under the same table (a JS capsule ID range, registered by `lambda/js` at
bootstrap): realm identity and well-known references; global bindings and
constructor caches; execution state (`this`, `newTarget`, super/with/eval
stacks); event-loop queues and timers; Promise/unhandled-rejection state;
module/CommonJS state; each Node module with mutable realm state; the
Test262 agent; diagnostics. `JsDeferredMirState` becomes a `JsCodeStore`
capsule of `CONTEXT` lifetime owning dynamic `JsCompiledArtifact` records
(MIR context, source owner, module image, release), which makes "preamble
code survives a realm reset" an explicit lifetime rather than a skipped
entry. No `JsNodeState` or `JsWebState` grouping capsule is introduced;
unrelated subsystems get unrelated slots.

**JSCU17 — DOM and web state are context capsules, not JS sub-capsules.**
Event, observer, XHR, history, collection, foreign-document, fetch and
canvas state register under `lambda/dom`'s own ID range with `REALM`
lifetime and are constructed by whichever language first touches them.
`js_runtime_state.dom_*` and the three ensure spellings are deleted; the
DOM code reaches its state through `context_capsule`. The Node session
keeps its generation-token semantics and becomes the capsule behind one ID.

**JSCU18 — The three lifecycle contracts are table walks.** Full batch
reset: quiesce resources, assert execution capsules empty, walk `REALM`
capsules in reverse registration order calling `release_heap` then
`destroy`, replace the heap/name-pool/module slabs, construct on demand.
Preamble checkpoint: a typed `JsRealmCheckpoint` (module slab ids and
counts, cache epochs) restored by the capsules that promised to honour it;
it never walks "all roots". Context destroy: walk every capsule calling
`destroy`. `js_runtime_state_release_heap_resources`,
`js_runtime_state_destroy_context` and `js_root_range_reset_all` have no
body after this item.

### 3.4 What does not change

- `EvalContext : Context` inheritance and the frozen prefix stay (Lambda
  parent SCUO3); the directory is appended, never inserted.
- The `js_runtime_state` accessor macro and `js_active_runtime_state` TLS
  cache stay during migration; hot-path field macros are retired only after
  their users read the capsule.
- nextTick, microtask, timer, immediate and RAF stay **named queues** in
  the event-loop capsule (D6.3.1, parent §7.6); only their handle storage
  moves to `RootVector`.

### 3.5 Acceptance

- Ratchet: `sizeof(JsRuntimeState)` ≤ 1,024 B; `void*` fields on
  `JsRuntimeState` = 0; `mem_calloc(1, sizeof(Js…RuntimeState)` sites = 0;
  `JS_DEFERRED_MIR_MAX` 0 hits.
- Two contexts constructed and destroyed in one process with an allocation
  census: capsule count on an empty realm, a core-only script, a DOM page
  without JS, a Node script, and a traced run, recorded as a table in the
  test log.
- Batch reset and preamble checkpoint repeated across ten heap epochs under
  forced GC, with the Test262 batch baseline unchanged.
- WPT slices `test_wpt_dom_events`, `test_wpt_dom_nodes`,
  `test_wpt_resize_observer`, `test_wpt_intersection_observer` at baseline;
  `make test-radiant-baseline` at baseline (the DOM capsules now live on the
  context Radiant constructs).
- Hot-path check: `context_capsule` compiles to one TLS load and one
  indexed load in the release build (inspect the MIR emission ratchet for
  the JS call kernel; MT7 slack unchanged).

---

## 4. One `FunctionCode` / value split shared by both languages

### 4.1 What the rulings already say

D6.2.1: a function value is a GC object carrying its entry pointers, its
signature and its closure environment; identity is the **static definition
site**. D6.2.2v2: per-callee call and construct entries; catalog IDs, names
and lengths are metadata and never select semantics. D5.4.1: one canonical
context; after SCU14/SCU15 no per-value context pointer is needed. Parent
JSCU6: immutable code facts are shared; mutable value state is not. Lambda
parent §6.3: `FunctionCode` / `FunctionValue`, "per-function
`runtime_context` disappears once D5.4.1's canonical TLS context is
universal; it must not be removed earlier." It is universal as of
2026-09-07.

### 4.2 What the tree actually does

- `Function` (`lambda/lambda.h:1336`, 72 B) and `JsFunction`
  (`lambda/js/js_function.hpp`, 328 B) both carry `LMD_TYPE_FUNC`. So does
  `JsAccessorPair` (`lambda-data.hpp:301`). The collector's `FUNC` arm
  (`gc_heap.c:1874`) first calls the `js_function_trace` hook;
  `js_function_gc_trace` (`js_runtime_function.cpp:190`) tells the three
  layouts apart by a 32-bit magic at byte 4 and returns 0 for the Lambda
  layout, which then falls through to the `closure_field_count` walk. Two
  tracers, one tag, a magic byte as the type system.
- Both records mix the two lifetimes. Code facts on `Function`: `fn_type`,
  `ptr`, `entry_abi`, `name`, `def`, `def_module`, `method`, the
  `mir_public_return_shape` bits. On `JsFunction`: `func_ptr`,
  `param_count`, `formal_length`, `invoke`, `construct`, `native_call`,
  `native_construct`, `native_target`, `native_arity`, `native_policy`,
  `catalog_id`, `intrinsic_class`, `typed_array_element_type_plus_one`,
  `module_state_id`, `source_text`, `vm_stack_*`, `ast_function`,
  `ast_script`, the four `ast_*` facts, `body_kind`. Value state on both:
  the closure environment and `runtime_context`; on `JsFunction` also
  `prototype`, `properties_map`, `home_global`, `home_class`, the bound
  triple, the class triple, `with_env`, `interp_env`, the two lexical
  Items.
- Since the parent census, `JsFunction` gained the AST-body fields and grew
  from 280 to 328 B. Every closure created in a loop pays the class,
  bound, eval-origin and AST fields it does not use.
- Pool-backed (non-GC) JS functions register four raw roots each for
  `name`, `source_text` and the two `vm_stack_*` strings
  (`js_runtime_function.cpp:134`) — code facts rooted per value because
  the value is the only owner.
- Pinned offsets: `func_ptr` at 8 and `bound_this_store` at 48 are
  static-asserted (`js_function.hpp:120–122`) as "the compiled-function
  ABI" and "the shared ABI"; `Function` pins `type_id` at 0,
  `closure_field_count` at 2, `fn_type` at 8, `closure_env` per
  `LAMBDA_GC_OFF_FUNCTION_CLOSURE_ENV`. No `offsetof(JsFunction` reader
  exists in the JS MIR emitters; the consumers of the two JS pins are the
  generated call entries and must be inventoried first (JSCUO6).
- The return contract is already shared: `FnReturnAnalysis` (SCU11) is
  referenced, not copied, by `MirFunctionPlan` and `JitCallMetadata`; JS
  reads its facts through `jm_function_analysis`. The code record below has
  its ABI half for free.

### 4.3 Rulings

> **JSCUO6 CLOSED and JSCU20 (first payload) LANDED 2026-09-08.**
>
> The inventory the design demanded before item 4 moves a field is now an
> enforced assert block in `js_function.hpp`, and it corrects the design's
> risk assumption. Three records share the `LMD_TYPE_FUNC` tag and the
> `Item::function` slot -- Lambda's `Function`, `JsFunction` and
> `JsAccessorPair` -- and every consumer discriminates by reading `type_id`
> at 0 then `layout_magic` at 4. **Those two offsets are the only hard
> constraint on `JsFunction`.** No generated code reads a `JsFunction` field:
> JS MIR lowers every call to a named C helper and dispatches through
> `fn->invoke` in C, unlike Lambda's `Function`, whose `flags` word
> `transpile-mir.cpp` loads at a baked `offsetof`. The collector reaches
> `JsFunction` only through the `js_function_trace` hook, so the raw
> `LAMBDA_GC_OFF_FUNCTION_*` path applies to `Function` alone. The two
> historical pins (`func_ptr` at 8, `bound_this_store` at 48) back no reader
> this inventory could find and are kept as tripwires, not as an ABI
> contract. **Item 4's `JsFunction` half is therefore much less
> ABI-constrained than §4.2 assumed.**
>
> The first optional payload follows: `JsEvalOrigin` (filename, source, line
> and column offsets) is allocated only for a dynamically compiled function,
> so an ordinary closure carries one null word instead of four fields.
> `sizeof(JsFunction)` **328 → 304 B**. Two supporting facts made this more
> than a field move: function values had **no finalizer** (`external_destroy`
> had no `LMD_TYPE_FUNC` arm), so any native payload would have leaked -- a
> `js_function_destroy` GC hook now exists beside `js_function_trace` and
> `js_function_compact`, which is the lifecycle every later payload needs;
> and pool-backed root registration is one-shot, so an origin attached after
> finalization would never have been rooted -- the setter roots the payload's
> own slots instead of re-entering that path.
>
> Three more payloads follow the same shape: `JsBoundData` (target, owned
> argument vector, count), `JsClassData` (constructor, instance prototype,
> superclass) and `JsWithData` (captured `with` env and depth). Each is
> absent on an ordinary closure. Reads go through null-safe accessors
> (`js_fn_bound`/`js_fn_class`/`js_fn_with`) that return a shared zeroed
> record when the payload is missing, so an unguarded read that was safe as
> an inline field stays safe; only writes allocate. `sizeof(JsFunction)`
> **328 → 264 B**.
>
> Gates: test262 baseline **0 regressions** (40261/40261); rooting gate exit
> 0; JS gtest **370/370**; the callable-catalog gate clean; `new Function`,
> error stacks, `vm.runInThisContext`, a 150-iteration dynamic-function
> churn, two-level `bind` chains with correct `.length`, class inheritance
> through `super`, and `with`-scope capture all correct under
> `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
>
> **JSCU20 AST payload LANDED 2026-09-08.** The AST group is now one optional
> `JsAstBody*`: `function`, `script`, `env`, `lexical_this`,
> `lexical_new_target` and the three derived flags move behind a pointer that
> only an AST-bodied closure allocates. `body_kind` stays inline because it is
> the discriminator every call site tests. Reads go through the same null-safe
> accessor shape as the other payloads (`js_fn_ast` returns a shared zeroed
> record), and because that accessor returns `const`, a clean build is itself
> the proof that every write path uses `js_fn_ast_ensure` — the compiler
> rejects a store through the shared absent record. 69 sites were rewritten
> across `js_interp.cpp` and `js_runtime_function.cpp`; the `state->` and
> `gen->` fields of the same name belong to the generator and async state
> records, not to `JsFunction`, and were correctly left alone.
>
> The payload's two Items are precise GC edges, so `js_function_trace` follows
> the pointer and `js_function_gc_destroy` releases it. A pool-backed
> `JsFunction` is never traced and its root registration is one-shot, so
> `js_function_root_ast_payload` registers the payload's own Item slots at
> attach time — the same contract `js_function_set_eval_origin` already uses.
>
> `sizeof(JsFunction)` **264 → 232 B** (the group's 5 pointers/Items and 3
> flags cost 40 B inline; one pointer replaces them). Gates: test262 baseline
> 0 regressions (40261/40261), JS gtest 370/370, JS script gtest 107/107
> (which is the suite that exercises `JS_EXECUTION_BACKEND=ast`), rooting core
> 107/107, MIR GC stress 107/107, GC heap 76/76, callable catalog clean, node
> slice identical to pristine. Closures, arrow `this`, `super`, generators,
> `arguments`, direct `eval` and a 300-iteration closure churn all produce
> identical results on the MIR and AST tiers, with and without
> `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
>
> ⚠ A stale test binary made this look like a segfault regression at first:
> `make build` does not rebuild `test/*.exe`, so a suite run before
> `make build-test` links against the previous struct layout. Rebuild the tests
> after any layout change before believing a crash.
>
> **What remains of item 4.** `runtime_context` is *not* removable after all:
> it is the `Context*` actually handed to generated code on the
> `MIR_CONTEXT_ABI` path, and its guard needs the captured owner to catch a
> wrong-thread invocation -- the same finding as JSCU9's generator guard, and
> kept for the same reason. The AST group (`ast_function`, `ast_script`,
> `interp_env`, the two lexical Items and three flags, 42 JsFunction sites
> concentrated in the interpreter) is a further ~48 B. Reaching the ≤ 160 B
> ratchet then needs **JSCU19 itself**: the shared `FunctionCode` /
> `JsCallableCode` record for the immutable code facts, which is item 4's
> centrepiece, touches the call kernel, and wants the code identity to be
> shared across closures of one source rather than copied per value. That is
> a substantial change of its own and is deliberately left as the next unit
> rather than rushed onto the end of this one.

> **JSCU19 stage A LANDED 2026-09-08 — native code facts split off the value.**
> `JsNativeCode` (`call`, `construct`, `target`, `arity`, `policy`) is the
> native half of the `JsCallableCode` record. 51 sites across six files moved
> to it, using the same null-safe accessor shape as the other payloads, so a
> read on a script closure still sees a zeroed record and only writes allocate.
> `sizeof(JsFunction)` **232 → 216 B**.
>
> **Two corrections to §4.3's premises, both found by measurement.**
>
> *The sharing premise does not apply to the native half.* JSCU19 justifies the
> record by sharing it across every value of one code identity. But
> `js_func_cache` already dedupes the **values**: its key is
> `(target_bits, arity, kind, policy, capabilities)` — which is precisely the
> native group — so two wrappers of one C target are the *same* `JsFunction`,
> and sharing a record between them is a no-op. What this split actually buys
> is that a script closure carries one null word instead of 32 B of zeros. The
> native half of JSCU19 therefore pays as an **optional payload**, not as a
> sharing mechanism. Sharing remains the right justification for the *script*
> half, where N closures are genuinely created per evaluation of one source.
>
> *The ≤ 160 B ratchet is not reachable as specified.* Two of JSCU19/JSCU20's
> sub-points are already superseded: `runtime_context` cannot be removed (it is
> the `Context*` handed to generated code on `MIR_CONTEXT_ABI`, JSCUO6), and
> `layout_magic` must stay a 32-bit field at offset 4 (one of only two hard
> pins). With those kept, and `invoke`/`construct`/`func_ptr` retained on the
> value as JSCU21 requires, the floor for the field moves §4.3 lists is about
> **196 B**, not 160.
>
> **The route that does reach it is JSCU20's own wording.** JSCU20 specifies
> *one* `JsFunctionPayload*`; the implementation landed **six** separate
> pointers (`bound`, `klass`, `with`, `ast`, `native`, `eval_origin` = 48 B).
> Collapsing them into a single payload pointer recovers 40 B and brings the
> record under the ratchet. The trade is one indirection on every payload read
> and one allocation covering all six, which suits a value that has either none
> of them or several. **JSCUO8 (open):** confirm that collapse is wanted before
> spending it, since it re-touches every accessor landed in JSCU20.
>
> Gates: test262 40261/40261 with 0 regressions, JS gtest 370/370, JS script
> gtest (the `JS_EXECUTION_BACKEND=ast` suite) clean, rooting core 107/107, MIR
> GC stress clean, callable catalog clean, both Jube node gates clean, node
> slice identical to pristine, and all 26 built-in modules still byte-identical
> under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.

> **JSCUO8 RATIFIED and LANDED 2026-09-08 — one payload word, not six.**
> `JsFunctionPayload` holds the six optional records (`bound`, `klass`, `with`,
> `ast`, `native`, `eval_origin`) and the value carries a single pointer to it.
> The change was contained because JSCU20's accessors already hid the pointers:
> only the tracer, the destroy hook, the `ensure` helpers and two sites in
> `js_runtime.cpp` touched them directly. Accessor signatures are unchanged, so
> no call site moved. `sizeof(JsFunction)` **216 → 176 B**, and the payload side
> of the tracer is now one null check instead of five.
>
> **The trade is better than the byte count suggests, and for a different
> reason than the ratchet.** An ordinary compiled closure has *no* payload at
> all — no `native` (not a wrapper), no `ast` (compiled body), and
> `bound`/`klass`/`with`/`eval_origin` only in the cases that name them. It
> therefore allocates no container: the unbounded population of script closures
> shrinks by 40 B and pays nothing. The bounded population of native builtins,
> which each hold exactly one record, pays one 48 B container apiece, once per
> realm.
>
> **JSCUO9 RESOLVED 2026-09-08 — the ratchet is restated as ≤ 128 B and met.**
> `sizeof(JsFunction)` is now **exactly 128 B** and the record allocates from
> the 128 B GC slot. Three changes got the last 48 B:
>
> - **`bound_this_store[2]` moved into `JsBoundData`** (−16 B). Only a bound
>   function has a bound receiver, and the setter is already called one line
>   after `js_fn_bound_ensure`, so the home belongs with the rest of that state.
>   JSCUO6 had kept the offset-48 pin as a tripwire and said moving the field
>   was safe once its inventory block was updated in the same change; it was.
> - **`home_class` moved into the payload** (−8 B). Only a method carries one.
> - **The remaining fields are ordered by alignment** (−24 B). The old source
>   order left six 4- and 1-byte fields each sitting in an 8-byte hole: 30 B of
>   interior padding in a 152 B record. This is pure layout and changes no
>   semantics.
>
> **`source_text` cannot move, and the NO_GC audit is what proved it.** Putting
> it in the payload made `js_set_function_source` — a `JIT_EFFECT_NO_GC` import
> in `sys_func_registry.c` — able to allocate, because a lazily minted payload
> allocates. `make test-gc-rooting-core` failed the transitive audit
> immediately. The field stays on the value; `home_class` was taken instead
> because its setter `js_set_function_home_class` is `JIT_EFFECT_MAY_GC`.
> **A field's mobility is decided by its setter's JIT effect class, not by how
> optional the field looks.**
>
> **The allocation class was also stale, and that is most of the win.**
> `JS_FUNCTION_SIZE_CLASS` selects a slot from `gc_object_zone.c`'s table; it
> is that constant, not `sizeof`, that the allocator honours. It was **7 — the
> 384 B slot** — which the record genuinely needed at 328 B when item 4 began,
> but every shrink after that kept handing out 384 B slots. It is now class 5.
> So the measured result is **384 B of slot per function value → 128 B**, a
> third of the previous footprint, and the earlier field moves only convert
> into a saving now that the constant tracks the size. Keep the constant and
> the `static_assert` moving together.
>
> Gates: test262 40261/40261 with 0 regressions, JS gtest 370/370, JS script
> gtest clean, rooting core 107/107 **including the NO_GC transitive audit**,
> MIR GC stress clean, GC heap clean, callable catalog clean, both Jube node
> gates clean, node slice identical to pristine, 26/26 built-in modules
> identical under forced GC, and `bind`, `super`, `new Function`, `with`,
> `toString` on plain/class/bound/native functions and both execution tiers all
> identical with and without forced GC.

> **JSCUO9 (original) — the ≤ 160 B ratchet is measured against the wrong scale.**
> The GC object zone allocates in classes of 16, 32, 48, 64, 96, 128, 256 and
> 384 B (`gc_object_zone.h`). **216 B and 176 B both land in the 256 B class,
> and so does 160 B.** The ratchet as written therefore buys nothing at the
> allocator; the only threshold that changes a function value's real footprint
> is **≤ 128 B**, which halves the slot. From 176 B that needs 48 B more:
> dropping the `bound_this_store[2]` tripwire recovers 16 (JSCUO6 found it
> backs no reader), and moving the remaining script code facts into
> `JsCallableCode` — `source_text`, `param_count`, `catalog_id`,
> `module_state_id`, `formal_length`, `intrinsic_class`, the typed-array
> element byte, `body_kind` and `eval_initializer_context`, about 34 B of
> fields behind one pointer — recovers roughly 26 more, landing near 134 B.
> Closing the last few bytes would mean moving `invoke`/`construct` off the
> value, which JSCU21 deliberately keeps there as cached projections for call
> speed. So ≤ 128 is reachable only by re-opening JSCU21, and the ratchet
> should be restated as ≤ 128 with that dependency named, or dropped as a
> target in favour of the per-population argument above.
>
> Gates: test262 40261/40261 with 0 regressions, JS gtest 370/370, JS script
> gtest clean, rooting core 107/107, MIR GC stress clean, GC heap clean,
> callable catalog clean, both Jube node gates clean, node slice identical to
> pristine, 26/26 built-in modules identical under forced GC, and `bind`
> chains, class `super`, `new Function`, `with` capture and a 200-iteration
> bind churn all identical with and without
> `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.

### 4.3 Rulings (original)

**JSCU19 — One `FunctionCode` header, extended by `JsCallableCode`.**
Non-GC, immutable after publication, owned by the module artifact (Lambda
`Script`; JS `JsCompiledArtifact` from JSCU16) so it outlives every value
that references it:

```c
typedef struct FunctionCode {
    TypeFunc*                 signature;
    const FnReturnAnalysis*   abi;          // SCU11; never copied
    const FnEffectSummary*    effects;      // SCU12
    fn_ptr                    checked_entry;
    fn_ptr                    boxed_entry;  // the universal `_b` target (D6.2.1)
    const char*               name;
    FunctionSite              site;         // (module, definition node) — D6.2.1 identity
    FunctionEntryAbi          entry_abi;
    uint8_t                   arity;
    uint32_t                  flags;
} FunctionCode;

typedef struct JsCallableCode {
    FunctionCode              base;
    JsCallEntry               invoke;       // for the value's cached projection (JSCU21)
    JsConstructEntry          construct;    // absent = not a constructor (D6.2.2v2)
    JsNativeCallBody          native_call;
    JsNativeConstructBody     native_construct;
    JsNativeTarget            native_target;
    uint8_t                   native_arity, native_policy;
    uint8_t                   intrinsic_class, typed_array_element_type_plus_one;
    int16_t                   formal_length;
    int32_t                   catalog_id;
    uint32_t                  module_state_id;
    uint8_t                   body_kind, ast_facts;   // direct-eval / arguments / tail-reuse
    const JsEvalOrigin*       eval_origin;  // NULL unless dynamically compiled
} JsCallableCode;
```

`JsEvalOrigin` holds the VM filename, source text and offsets once per
compilation, so the four per-value raw roots at
`js_runtime_function.cpp:134` disappear with the fields they root.

**JSCU20 — One value record per language, value state only.** `Function`
keeps its pinned prefix (`type_id`, `arity`, `closure_field_count`,
`entry_abi`, `flags`, then `code`, `ptr` as a cached projection of
`code->checked_entry` for generated code, `closure_env`, `name` cached);
`runtime_context`, `def`, `def_module` and `method` move to the code
record. `JsFunction` becomes:

```c
struct JsFunction {
    TypeId               type_id;      // LMD_TYPE_FUNC
    uint8_t              layout_kind;  // FUNCTION_LAYOUT_JS; replaces the 32-bit magic
    uint16_t             flags;        // per-value bits only: HAS_BOUND_THIS, USES_WITH, …
    const JsCallableCode* code;
    void*                func_ptr;     // cached projection of code->base.checked_entry (pinned 8 → re-pinned, JSCUO6)
    Item*                env;  int env_size;
    JsCallEntry          invoke;       // cached projection (JSCU21)
    JsConstructEntry     construct;    // cached projection
    Item                 prototype, properties_map, home_global, home_class;
    Item                 bound_this_store[2];                 // pinned 48, kept
    JsFunctionPayload*   payload;      // NULL for ordinary closures
};
```

Optional payloads, allocated only for the values that need them, each a
small GC object traced by the value: `JsBoundFunctionData` (target, owned
argument vector), `JsClassFunctionData` (constructor body, instance
prototype, superclass), `JsWithFunctionData` (`with_env`, depth),
`JsAstFunctionData` (`interp_env`, lexical `this`, lexical `newTarget`).
Target `sizeof(JsFunction)` ≤ 160 B; `runtime_context` is gone from both
languages.

**JSCU21 — `invoke`, `construct` and `func_ptr` on the value are cached
projections with one writer.** `js_function_finalize_capabilities`
(`js_runtime_function.cpp:154`) is already the single writer of the
capability pair under D6.2.2v2; it now reads `code` plus the value's bound
flag and payload and writes the three cached fields. This is the same
pattern SCU13 adopted for `Function::mir_public_return_shape`: generated
code keeps its fixed-offset read, the fact has one origin.

**JSCU22 — One tracer arm, dispatched on `layout_kind`.** The `FUNC` arm
reads the layout byte and traces the Lambda value (env by
`closure_field_count`), the JS value (env, four realm Items, bound-this,
payload) or the accessor pair. `js_function_trace` and
`js_function_compact` hooks and the two magic constants are deleted; code
records are never traced because they are never GC objects.

### 4.4 What does not change

- `FnReturnAnalysis`, `FnEffectSummary` and `FnAnalysis` are untouched;
  the code record points at them.
- Catalog aliases still control `===` (parent §8): two values may share one
  `JsCallableCode` without being the same value.
- `JsAccessorPair` stays a FUNC-tagged two-Item record; it gains the layout
  byte and loses its magic.
- The realm function cache keeps its semantics (same MIR function → same
  wrapper); its storage moves to `RootVector` under item 2.

### 4.5 Acceptance

- Grep gate: `runtime_context` 0 hits on `Function` and `JsFunction`;
  `JS_FUNCTION_LAYOUT_MAGIC` and `JS_ACCESSOR_PAIR_LAYOUT_MAGIC` 0 hits;
  `js_function_trace` 0 hits in `gc_heap.c`.
- `sizeof(Function)` ≤ 72 B, `sizeof(JsFunction)` ≤ 160 B, both
  static-asserted beside the pinned-offset asserts; the JSCUO6 inventory is
  checked in as a static-assert block before any field moves.
- MIR emission ratchet (MT7) unchanged at 0 % slack in both the Lambda and
  JS kernels; `make interp-sweep` mismatch 0.
- Test262 callable coverage: `make test-js-callable-catalog`, plus targeted
  cases for bound-of-bound, class constructors with and without `extends`,
  `IsConstructor` on every builtin kind, `new.target` through a bound
  function, eval-origin stack frames, and a builtin alias pair under `===`.
- Release-build call/`new` micro-benchmark and the JS AWFY subset within
  noise of the archived control.

---

## 5. Two banked fixes

### 5.1 `ParsedRequest` becomes spans (D1.9)

> **Status: LANDED 2026-09-07 (JSCU23).** `ParsedRequest` (815,168 B, an
> automatic variable at three call sites) is replaced by `HttpRequestHead`
> (< 256 B) plus `HttpFieldSpan`/`HttpFieldList` growable span lists that
> index the connection buffer; the lowercase key is derived at
> materialization, not stored twice. The 64-header / 32-trailer / 4 KB caps
> are gone; the header section is bounded by `HTTP_MAX_HEADER_SECTION`
> (16 KiB, Node's default) which fails a request 431 instead of truncating.
> `http_header_has_token` gained a length-aware core so views without a NUL
> work. One head is reused across the pipelined requests of a read callback
> and released at the end. Gate: the Node official HTTP slice was **16/205
> pass on true master → 137/205 with this change, 0 regressions**; the span
> parser's own on/off diff is identical (137/137), so it introduces nothing.
> The 121-test jump came from a **pre-existing CommonJS regression** fixed as
> a blocker (require returned `{}`): the CJS `module` object is now a second
> descriptor fact (`ModuleDescriptor::cjs_module`) instead of a doomed
> `source_lang` retag of `namespace_obj`. The 68 still-failing HTTP tests are
> a separate pre-existing subset of master's 189, out of scope here.

`ParsedRequest` (`lambda/js/js_http.cpp:296`) holds 64 raw and 64
normalized header pairs plus 32 and 32 trailer pairs at 128 + 4,096 bytes
each: 815,168 B. It is declared as an **automatic variable** at `:2809`,
`:3114` and `:3145`, so every request parse places 815 KB on the native
stack, and header 65 is silently dropped.

**JSCU23.** `HttpRequestHead` keeps method/version/URL/body spans and two
`ArrayList`s of `HttpFieldSpan { uint32_t name_off, name_len, value_off,
value_len; uint32_t name_hash; }` into the connection buffer, which the
request already borrows (`body` is a `const char*` into it today). Raw
spelling, order and duplicates are preserved by the span; the normalized
name is derived on lookup or cached as a hash. `HttpRequestParser` owns
the cursor, the byte/count limits and the error status; limits stay
explicit checked policy that fails with the HTTP status the protocol
requires. `sizeof(HttpRequestHead)` ≤ 256 B excluding lists. Gate: the
existing `http` Node baseline slice plus a case with 100 headers, 40
trailers, a duplicated `Set-Cookie`, and a 16 KB header line.

### 5.2 Crypto handles use the Jube rid table (D7.4.1v2)

`JsCryptoNativeState` (`lambda/module/node_core/node_runtime_state.hpp:71`)
holds four `void*[4096]` tables (131 KB) for hash, HMAC, sign/verify and
cipher contexts. The Jube registry already owns **generation-checked
resource ids** with a close hook: `jube_node_resource_add_with_close`,
`_remove_for_session`, `_user_data_for_session`
(`lambda/jube/jube_registry.h:64–68`), which is exactly the D7.4.1v2 "system
resources are integer rids" table.

**JSCU24.** Each crypto context is added to the session's resource table
with its destructor as the close hook; the JS object stores the rid in its
internal slot; lookups go through `jube_node_resource_user_data_for_session`
with a kind check; session teardown closes remaining entries. The four
tables and `JS_CRYPTO_MAX_LIVE_CONTEXTS` have no definition after this
item. Hash, HMAC, sign/verify and cipher remain distinct resource kinds.
Gate: `make test-jube-node-net-crypto-dynamic` and the crypto slice of
`make node-baseline`, plus a case creating 5,000 live hashes.

**LANDED 2026-09-08 (JSCU24).** `JubeNodeResource` gained `bool is_handle` so
the rid table can carry entries that are *not* user-visible handles;
`jube_node_resource_add_native` opens such an entry and
`jube_node_resource_close_kind` drains one kind at teardown. Both
`_getActiveHandles` and `_getActiveResources` skip `!is_handle`, so crypto
contexts are invisible to the Node handle views — which is the observable
Node behaviour and was the reason a plain `add_with_close` would not do.
Each context struct carries a `uint32_t rid`; `X_ctx_close` is the close hook;
`X_ctx_free` closes through the rid when it has one and otherwise releases
directly, so a context freed before registration still cleans up. All
create/decode/free sites go through `crypto_resource_open/get/close`; the four
fixed tables, their tracking helpers, their drain loops and
`JS_CRYPTO_MAX_LIVE_CONTEXTS` are deleted. `JsCryptoNativeState` is
131,112 B → 1 B, and the 16,384-context cap is gone.

**A latent GC bug fell out of this and is fixed.** The four factories
(`js_hash_make_object` and the HMAC, sign/verify and cipher equivalents)
built a fresh object with `js_new_object()` and then installed its methods
through a run of allocating stores while holding only a bare C local. The
object is not reachable from anywhere else at that point, so under
`LAMBDA_GC_FORCE_EVERY=1` it was collected mid-construction and the finished
object came back missing methods ("is not a function") or with a reused
context ("Digest already called"). This predates JSCU24 — pristine `HEAD`
fails the same probe, with the second symptom — and it violates D5.4.2:
a value under construction is live and needs an exact root. Each factory now
opens `JS_ROOTS(obj_roots, obj_root, obj)` immediately after `js_new_object()`
and performs every subsequent store through `obj_root.get()`, returning the
rooted handle. Hash, HMAC and cipher round-trips plus a 200-iteration churn
now produce byte-identical results with and without forced GC.

Verified: `make test-jube-node-net-crypto-dynamic` clean, `test262-baseline`
40261/40261 with 0 regressions, `test-gc-rooting-core` 107/107,
`test-js-callable-catalog` clean, JS gtest 370/370, and the crypto Node slice
at 65 pass / 67 pre-existing failures — identical to pristine, 0 regressed.
All four affected `modules/node-*.dylib` were rebuilt on both sides of the
A/B (`node-zlib` includes neither changed header and is unaffected).

---

## 6. Dependencies, ordering and gates

```text
item 5 (banked)   ─┐  independent; land first
item 1 (suspend)  ─┤  independent; touches js_runtime.cpp generator/async
                   │  kernels, the Map-carrier tracer, (JSCU25) the task
                   │  create path and (JSCU26, after JSCU25) the frame
                   │  storage in concurrency.cpp — own branch
item 2 (roots)    ─┤  independent of 1; adds a runtime primitive, then
                   │  migrates concurrency.cpp first, JS second
item 3 (capsules) ─┤  needs 2 (lifecycle owns roots); 1 makes the
                   │  ratchet reachable; touches EvalContext → Radiant gate
item 4 (callable) ─┘  needs 3 (JsCodeStore owns code records) and the
                      JSCUO6 offset inventory; touches both call kernels
```

Items 1 and 2 may be in flight at once; they edit different files. Item 3
must not start until item 2's JS migration (b)–(d) is merged, or the
capsule lifecycle inherits the reset registry it is meant to delete. Item
4 is last and alone.

Every item's gate includes `make build`, `make test-lambda-baseline` at
100 %, `make test-js262-prelim`, `make test262-baseline` and `make
node-baseline` at their recorded baselines (D7.3.5), plus the item-specific
checks above. Items 3 and 4 additionally run `make test-radiant-baseline`.
Items 1 and 4 run the release-build JS throughput comparisons against the
archived control (rule 10; one benchmark run at a time; verify the release
binary was not clobbered by a test build). Each item ends with a struct
census re-run (`python3 utils/struct_census.py -j 1 --top 50`) and the
numbers recorded in the parent proposal's §2 table, which is stale today.

Because rule 17 requires it: the one ruling change in this document,
D5.1.1→v2 for JSCU26, was ratified and written into the spec (1.51.0) and
the stack-frame record on 2026-09-07 before any code moved. No other item
changes an `S#`/`D#` ruling.

---

## 7. Open issues
**JSCUO5 — web-object fixed capacities. Observers LANDED 2026-09-08.**
`JsObserverRuntimeState` **1,598,480 → 32 B**, `JsObserverState`
**24,976 → 152 B**, and both capacities (`JS_OBSERVER_CAP` 64,
`JS_OBSERVER_TARGET_CAP` 32) are deleted.

The two levels needed different treatments, and the reason is the rooting:

- **Observers are held by pointer, individually allocated.** Each observer's
  `object` and `callback` are *registered GC roots*, so the array holding them
  must be address-stable — growing an array of embedded records by `realloc`
  would leave every registered root dangling. Growing an array of pointers
  moves nothing the collector knows about. Root registration also moved from
  "all 64 slots up front" to "this observer's two slots, at creation".
- **Targets are a plain growable array.** A `JsObserverTarget` holds DOM
  pointers and pinned refs but no `Item`, so nothing is registered against its
  address and the array may move freely.

Two hazards the change created and closed. `js_observer_deliver` deduplicated
owner documents in a stack array sized `JS_OBSERVER_CAP * JS_OBSERVER_TARGET_CAP`
— 2,048 pointers; with the caps gone that would have been unbounded, so it
grows on the heap. And `dom_observers_reset` zeroed the table with
`memset(observers, 0, sizeof(observers))`, which after the change memsets eight
bytes *through a pointer*: it segfaulted every mutation-observer UI fixture.
Both release paths now share one `observer_state_release_all`.
**When a fixed array becomes a pointer, `sizeof` on it silently changes
meaning — audit every `sizeof` and every stack array sized from the capacity.**

**All three web-object states are done.**

| Structure | Was | Now |
|---|---:|---:|
| `JsObserverRuntimeState` | 1,598,480 | **32** |
| `JsDomCollectionRuntimeState` | 753,696 | **72** |
| `JsXhrRuntimeState` | 72,720 | **24** |

`JsDomCollectionRuntimeState`'s four 4,096-entry registries grow on demand. They
look freely movable — nothing in an entry is a *strong* root — but each entry's
`array` is a **weak** GC slot registered by address, so `dom_collection_table_grow`
unregisters at the old addresses, moves, and re-registers at the new ones (and
restores the old registrations if the realloc fails). That kept all ~56 indexed
read sites unchanged.

`JsXhrRuntimeState`'s 64-record pool and each record's 64-header table both grow.
Nothing here is registered with the collector, so a plain realloc suffices.

**The same defect appeared in all three, and it is the thing to look for.**
When a fixed array becomes a pointer, `sizeof` on it silently becomes 8, and any
stack array sized from the deleted capacity becomes unbounded. Concretely:
`dom_observers_reset` and `reset_live_dom_collections` both cleared their tables
with `memset(table, 0, sizeof(table))` and segfaulted every DOM fixture until
they cleared by count instead; `js_observer_deliver` had a 2,048-pointer stack
array sized `JS_OBSERVER_CAP * JS_OBSERVER_TARGET_CAP`, and XHR's send path had a
64-entry `char* header_strs[MAX_HEADERS]` — both now sized to the live count.
**Grep every `sizeof` and every capacity-sized local before believing the
change.** The observer crash was caught by the UI fixtures, not by any JS or GC
gate.

`JsMirTranspiler` (30,840 B, 106 members) is a separate target and has grown
since the parent proposal. The largest remaining record in the tree is
`NodeRuntimeSession` at 805,648 B.

**JSCUO5 (original) — web-object fixed capacities are now the dominant pressure.** The
2026-09-08 census puts the three DOM web-object states at the top of the tree:
`JsObserverRuntimeState` 1,598,480 B (64 observers × 32 targets),
`JsDomCollectionRuntimeState` 753,696 B (four 4,096-entry registries, two with
identical entry layouts) and `JsXhrRuntimeState` 72,720 B (64 XHR records × 64
fixed request headers) — about 2.4 MB together, against `JsRuntimeState`'s
27,064 B after items 1–4. None was in scope for the four items. They are the
natural next unit and the same two mechanisms already proven here apply: a
GC-owned carrier or dynamic store in place of the fixed table, and a context
capsule for the lifecycle. `JsMirTranspiler` (30,840 B, 106 members) is fourth
and has *grown* since the parent proposal's table was written.

**JSCUO8 — RESOLVED 2026-09-08, ratified by the user and landed.** See §4.3.
The original question follows.

**JSCUO8 (original) — Collapse the six payload pointers into one `JsFunctionPayload*`?**
JSCU20 specifies a single payload pointer; the implementation landed six
(`bound`, `klass`, `with`, `ast`, `native`, `eval_origin`), costing 48 B on
every value. One pointer to a container recovers 40 B and is the only route
left to the ≤ 160 B ratchet, since `runtime_context` and `layout_magic` are
both pinned (JSCUO6) and `invoke`/`construct`/`func_ptr` stay on the value
(JSCU21). The cost is one extra indirection per payload read and a single
allocation covering all six, which suits a value that has either none of them
or several. It re-touches every accessor landed in JSCU20, so it wants an
explicit decision rather than being folded into the next slice.


- **JSCUO1 — ratified 2026-09-07 as JSCU25** (item 1).
- **JSCUO2 — ratified 2026-09-07 as JSCU26** (item 1); D5.1.1→v2 in
  spec 1.51.0, SF20 addendum in `Lambda_Design_Stack_Frame.md`.
- **JSCUO3 — Who constructs DOM capsules when no JS realm exists.** JSCU17
  says "whichever language first touches them"; the observer delivery path
  and XHR turn tokens today assume `js_active_runtime_state`. The Lambda
  DOM package (`lambda/dom`) is the test case: a Lambda page with a
  `ResizeObserver` and no script.
- **JSCUO4 — Three 512-entry closure snapshot forms.**
  `JsMirTranspiler::last_closure_*` (`js_mir_context.hpp:480`),
  `JsMirLastClosureSnapshot` (`js_mir_internal.hpp:110`) and
  `JsMirBranchState` (`js_mir_expression_lowering.cpp:6342`) copy the same
  arrays; one dynamic tracker with a mark/rollback checkpoint replaces
  them. Waits on the Lambda parent's SCUO4 so the two transpiler splits
  land together.
- **JSCUO5 — Observer, live-collection and XHR caps.** After item 3 makes
  them capsules: object-owned `JsObserver` with dynamic targets, one tagged
  `JsLiveCollectionRegistry` for the four 4,096-entry tables
  (`lambda/dom/dom.cpp:1970`), object-owned `JsXhrRequest`
  (`dom_xhr.cpp:67`). Parent §6.3, §6.4, §7.2 apply unchanged; D7.4.5v2's
  live `VArray` stays a later stage.
- **JSCUO6 — Inventory of generated-code readers of `JsFunction` offsets.**
  Two pins are asserted (`func_ptr` at 8, `bound_this_store` at 48) and no
  emitter spells `offsetof(JsFunction`; the readers are the generated call
  and construct entries. Before item 4 moves a field, list every reader in
  a static-assert block beside the layout, the way `LambdaGcFunctionLayout`
  pins `Function`.
- **JSCUO7 — Eval journal frame caps.** `JS_EVAL_*_MAX` (`:756–778`) size
  both the Item lanes (item 2 migrates them) and the semantic frame marks
  and flags. The frames become `ArrayList`s beside the vectors under
  JSCU13; whether a direct-eval nesting depth remains a checked policy
  limit is decided when the journals move.

---

## 8. Phase 2 — one concept/mechanism, one runtime data structure

### 8.1 Boundary of the rule

Two records are consolidated only when they have the same semantic identity,
owner, lifetime, trace/finalize contract, mutation protocol and ordering
semantics. If only their storage mechanism is shared, the shared mechanism or
header is unified while semantic tails remain distinct. This is D1.3's
guest-semantic boundary applied to data layout: reuse contracts below the
boundary, never inherit another concept's semantics by accident.

Therefore this phase does **not** aim at one large JS-state union. It removes
duplicate authorities:

- one directory owns context subsystem state;
- one activation record owns a synchronous JS call's ambient state;
- one realm slot store owns fixed rooted singleton/cache Items;
- one global-environment table owns global binding records;
- one job envelope and queue mechanism carry queued callbacks;
- one rid table owns native resources;
- one environment carrier owns outliving lexical storage;
- one immutable code record owns facts about a function definition.

Generator, async and Promise state machines remain distinct. So do nextTick,
Promise-microtask, timer and RAF ordering; Lambda and JavaScript function value
semantics; AST and MIR semantic walkers; transient property descriptors and
stored accessor cells; and operation-specific native request payloads. JSCU8,
D6.2.2v2, D6.3.1 and D8.1.3v10 require those distinctions.

### 8.2 Post-round-1 tree census

The 2026-09-09 current-tree layout probe changes the target. The first round
removed the dominant generator/async/web slabs and reduced `JsFunction`, but
several migrations stopped after replacing embedded records with pointers.

| Structure / mechanism | Current macOS layout | Residue |
|---|---:|---|
| `JsRuntimeState` | **9,120 B** | JSCU16's ≤ 1,024 B ratchet is still missed by 8,096 B |
| eagerly allocated JS realm records | **≥ 88,240 B measured** | `js_runtime_state_alloc_records` allocates seventeen records, including optional Node/Test262/readline/assert state |
| `NodeRuntimeSession` | **805,648 B** | trace, permissions and diagnostics are embedded even when unused |
| `NodeTraceState` | **528,928 B** | 64 categories and 2,048 events are inline |
| `JsPermissionPolicy` | **263,175 B** | two 128-entry tables each inline a `PATH_MAX` path |
| `JsDiagnosticsChannelState` | **8,832 B** | parallel 512-entry name/channel arrays plus 64 deferred errors |
| `JsFunction` | **128 B** | value size ratchet is met; immutable per-definition facts still have no shared `FunctionCode` owner |
| `JsMirTranspiler` / `JsMirFunctionEmitter` | **664 B / 1,560 B** | bulk is solved; duplicated cache/cursor/checkpoint shapes remain |

`EvalContext` still carries `render_map_state`, `template_state_store`,
`jube_node_session` and `js_state` beside `capsules[]`/`capsule_ops[]`.
`ContextCapsuleId` names only DOM/web capsules. `NodeRuntimeSession` adds a
second `module_states[]` directory. `JsRuntimeState` then acts as a third
directory and manually allocates/frees its records. That is one context-state
concept with three ownership mechanisms, contrary to D5.4.2–D5.4.4.

Root ownership has the same residue. `JsRootRange`, `JsRootedState`,
`JsNamespaceState`, the 64-entry reset registry and the hand-written
`JS_RUNTIME_ROOT_STORAGE` count catalog coexist with `RootVector`. The source
already records the failure mode: a range beginning at a derived field left
the inherited namespace Item unrooted and scanned one Item past the struct.
The layout-dependent count is the duplicated authority; the runtime record
itself should determine its live Item extent (D5.3.5).

Finally, ordinary semantic collections retain allocation-policy caps in their
type layouts: 1,024 timers, 1,024 RAF callbacks, 512 global-module bindings,
1,024 lexical bindings, 256 async hooks, 1,024 pending destroys, 256
AsyncLocalStorage instances, 256 readline input/interface pairs, and the
Test262/process/diagnostics tables. A language or protocol bound may remain a
checked policy; an implementation capacity does not belong in the semantic
record.

### 8.3 JSCU27 — the context capsule directory is the only subsystem owner

**Decision.** Replace the parallel `EvalContext::capsules[]` and
`capsule_ops[]` fields with one `ContextCapsuleDirectory`. Its per-context
state is only indexed slots and lifecycle/generation bits; a frozen global
descriptor table owns each ID's name, size, lifetime and operations, per
D5.4.4. An indexed read remains ordinary owner-thread loads with no lock,
atomic or repeated catalog search.

Move `render_map_state`, `template_state_store`, `jube_node_session` and
`js_state` behind IDs. Register separate IDs for JS execution state, realm
slots, global environment, code store, event loop, timers, Promise state,
Node session/core-module state, rendering/template state and the existing DOM
peers. `JsRuntimeState` is not a directory inside this directory; its small
irreducible remainder becomes `JsExecutionState`.

The descriptor declares a semantic lifetime scope: the existing `CONTEXT` or
`REALM`, or the new `NODE_SESSION`. Construction, quiesce, heap release,
heap replacement, checkpoint restoration, session detach and final destroy
are directory walks over the matching scope. This completes JSCU18's
"reset becomes construction" contract from D5.4.3.

Node modules obtain their registered slot through the opaque Jube host API.
They do not receive `EvalContext*`, and `NodeRuntimeSession::module_states[]`
is deleted; this preserves D7.4.3 while removing the child directory.

### 8.4 JSCU28 — one synchronous JavaScript call activation

**Decision.** Introduce one `JsCallActivation` for the ambient state of a
synchronous call:

- `this`, `newTarget`, callee and private home class;
- generator callee prototype where construction needs it;
- actual argument span, count, source span and strictness;
- the previous activation link and non-Item execution flags.

Its Item fields occupy precise side-root slots; POD fields live in the native
activation. `JsExecutionState` carries only the current-activation pointer.
Call and construct entries create one activation and every helper reads that
record instead of unrelated singleton fields. Return pops it once. Suspension
is the D5.1.3 re-homing barrier: only facts needed after the native frame
returns are copied into the suspended carrier.

This deletes `current_this`, `new_target`, `current_private_home_class`,
`generator_callee_proto`, `pending_call_args`, `pending_args_callee` and their
save/restore clusters from `JsRuntimeState`. It also completes D6.2.2v2:
`newTarget` is supplied to the construct capability and recorded in the
activation, never communicated through a pending one-shot side channel.

### 8.5 JSCU29 — one realm slot store and one global environment

**Decision A: singleton/cache Items.** Introduce `JsRealmSlots`, containing
one `RootVector` and a generated `JsRealmSlotId` catalog. It owns namespace
objects, prototypes, constructors, well-known objects, intrinsic callables
and small realm string caches. Optional slot ranges allocate RootVector blocks
on first use. Typed accessors preserve subsystem names; storage and rooting
have one authority.

Delete `JsRootRange`, `JsRootedState`, `JsNamespaceState`,
`root_range_registry[]`, `JS_RUNTIME_ROOT_STORAGE`, its numeric extents and
its builtin-cache reset exemption. Heap replacement clears the one store by
its declared lifetime; no reset walk rediscovers which fields are roots.

**Decision B: global bindings.** Consolidate `JsGlobalBindingState` and
`JsGlobalVarModuleBindingState` into one dynamic `JsGlobalEnvironment` with
one `JsGlobalBinding` row per declared binding. The row owns key, value or
module cell, storage kind, mutability and initialization state. Declarative,
object-backed and module-backed behavior remain explicit kinds; their key and
metadata no longer live in parallel arrays with independent counts.

The same row discipline applies locally where a pair is one concept:
readline input/interface becomes `JsReadlineInput`; diagnostics name/channel
becomes `JsDiagnosticsChannel`; RAF id/callback becomes a job record. It does
not justify a universal table for unrelated records.

### 8.6 JSCU30 — one queued-job envelope and queue mechanism

**Decision.** Define one `JsAsyncContextSnapshot` containing async resource,
AsyncLocalStorage context and domain, and one `RuntimeJob` containing callback,
argument pack, context snapshot, job kind and optional id. `RuntimeJobQueue`
is the single queue storage mechanism.

nextTick, Promise microtasks, unhandled-rejection work and RAF each instantiate
a separately named queue. Their priority, checkpoint and drain rules stay
separate under D6.3.1. A timer owns a `RuntimeJob` plus timer-only due/repeat
and libuv state; Promise reaction lists remain Promise semantic state but
materialize the same job envelope when scheduled.

This replaces the current split among four-Item `RuntimeAsyncDeque` rows,
RAF's callback/id parallel rings and the duplicated callback/resource/ALS/
domain fields in `JsTimerHandle`. The implementation may use reusable chunks
instead of one allocation per microtask, but the chunk layout must precisely
trace the `RuntimeJob` Item fields and the release-build queue benchmark is a
gate. Storage optimization may not recreate a second job record shape.

### 8.7 JSCU31 — one generation-checked runtime resource table

**Decision.** Promote the Jube Node generation-checked rid table to the
runtime-owned `RuntimeResourceTable`, one per context. A resource entry owns:

- generation-checked rid and immutable kind descriptor;
- script-visible owner `Item`;
- native payload and close/finalize operation;
- referenced, closing and completion state.

Timers, fetch work, filesystem handles, sockets, TLS handles and other native
resources use this table rather than private pointer arrays or indexes.
Operation-specific records retain their semantic tail but begin with one
`RuntimeAsyncRequest` header for context/rid, completion post, cancellation
and teardown. No string prefix selects executable behavior; the frozen kind
descriptor does.

Jube consumes the same table through its host API. Modules remain shielded
from the loop and cannot pump, poll or drain it. This is the concrete shared
micro-kernel required by D1.2 and D7.4.1v2–D7.4.2, and generalizes JSCU24
instead of adding another JS resource mechanism beside it.

### 8.8 JSCU32 — one environment carrier and one suspension prefix

**Decision A: environments.** Replace `GC_TYPE_JS_ENV` and
`GC_TYPE_JS_INTERP_ENV` with one tail-bearing `GcEnvironment` allocation and
one tracer. A layout descriptor declares the outer link, traced header Items,
Item-slot count, scalar-tail extent and semantic flags. Compiled Lambda,
compiled JS and interpreted JS may expose different accessors, but allocate
the same carrier and rely on the same trace/re-home contract.

This unifies storage, not capture semantics. Lambda captures immutable Item
snapshots while JavaScript captures mutable lexical cells by reference, as
D6.2.3v2 requires. D8.1.3v10 still permits distinct AST and MIR activation
records and semantic walkers.

**Decision B: suspension.** Extract the common execution/continuation fields
of `JsGeneratorStateRecord` and `JsAsyncContextStateRecord` into one
`JsSuspendedActivation` prefix: runtime owner, resume entry, environment,
state/program point, AST function/arguments/environments, replay storage and
continuation ownership. Generator and async carriers retain distinct semantic
tails and state machines. The one carrier/tracer owns all outliving Items and
scalar tail state under D5.1.1v2 and D5.1.3.

JSCU25's choice to represent a JS async activation as a weakly registered
`LambdaTask` is scheduler semantics, not a prerequisite for this storage
unification. JSCU26 may then reuse the carrier for `LambdaAsyncFrame` without
forcing generator, async and Lambda-task behavior into one state machine.

### 8.9 JSCU33 — one code authority; accessor cells are not functions

**Decision A: callable code.** Complete JSCU19 rather than treating the
128-byte `JsFunction` result as the end of the split. A construction-time
builder creates and seals one immutable `CallableCode` per static definition;
`JsCallableCode` extends it with distinct call/construct entries, body kind,
module identity, source and JS catalog metadata. `Function` and `JsFunction`
remain separate language value records containing value state and a code
reference. AST body facts belong to the retained Script/code owner, not every
closure made from that definition.

This gives D6.2.1's definition-site identity, executable entries and signature
one authority while preserving D6.2.2v2's separate JavaScript `[[Call]]` and
`[[Construct]]` capabilities. Any generated-code offset migration retains the
JSCUO6 static-assert inventory and lands only with release call/new gates.

**Decision B: accessors.** Replace `JsAccessorPair`'s fake `LMD_TYPE_FUNC`
layout with a dedicated internal `JsAccessorCell` carrier and trace rule. A
shape marked `JSPD_IS_ACCESSOR` stores the internal cell pointer; generic Item
or function operations never observe it. Its getter and setter Items are the
only outgoing edges.

An accessor pair is property storage, not a function value. The present
`layout_magic` branch makes every FUNC tracer/destructor responsible for three
unrelated layouts and makes safety depend on every consumer checking the
shape flag first. The dedicated carrier restores D2.4.1's single authority
and D6.2.1's meaning of a function value. Transient `JsPropertyDescriptor`
remains distinct because it describes an operation, not stored identity.

### 8.10 JSCU34 — one ArrayBuffer-view authority

**Decision.** Introduce a `JsArrayBufferView` header owning buffer pointer,
buffer Item identity, byte offset, explicit/tracking length and view kind.
`JsDataView` consists of that header; `JsTypedArray` extends it with element
type, Node Buffer brand and its derived `ArrayNum` view.

Detachment, resizable-buffer out-of-bounds state, live-length derivation and
`.buffer` identity read the common header. `ArrayNum` offset/length data is a
derived cache, not a competing authority. DataView and each typed-array brand
retain their distinct operations; only their ArrayBuffer-view mechanism is
unified (D2.4.1).

### 8.11 JSCU35 — consolidate compiler mechanisms, not semantic facts

The runtime changes above take priority. The remaining compiler duplicates
are smaller but are authority-drift risks:

1. Define one `JsMirNameCache` record and instantiate it for property Items
   and module NameIds. The result domains stay typed; the identical key,
   function-owner, count and 32-entry storage shape is written once.
2. Define one `JsMirCursor` containing current function item/function,
   `JsFuncCollected`, class, scope environment/count and scope depth. Nested
   function emission and branch lowering use one checkpoint/restore API;
   branch checkpoints add a variable-scope transaction and closure-journal
   mark instead of copying a second cursor shape.
3. Replace the method/static-field/instance-field/static-block arrays in
   `JsClassEntry` with one ordered tagged `JsClassMember` array. The member
   kind selects its method, field or static-block tail and preserves source
   evaluation order.
4. Make shared `BindingId`/`FnAnalysis` facts authoritative. Backend records
   retain only IDs and emitted artifacts instead of repeating name, binding,
   binding node and declaration-kind facts across `JsModuleConstEntry`,
   `JsNameSetEntry` and lowering maps.

These implement D2.4.1 and D8.2.4–D8.2.6. They do not merge compiler semantic
facts with emitted MIR state, and do not reopen the already-effective split
between the 664-byte module coordinator and 1,560-byte function emitter.

### 8.12 Landing order and acceptance

Recommended order:

1. **Bank isolated Node shrinkage.** Make trace categories/events,
   permission grants/paths, diagnostics rows/errors and the CommonJS cache
   directory dynamic and lazy. Read/write grants share one grant row with a
   permission-kind mask; paths are normalized owned strings, never
   `PATH_MAX` arrays.
2. **Land JSCU27.** Move the four direct `EvalContext` owners and the Node
   module slots into the one directory; make the lifecycle walks complete.
3. **Land JSCU28–JSCU29.** Remove call side channels and the private root
   registry before more state begins depending on either.
4. **Land JSCU30–JSCU31.** Move jobs first, then resource owners, preserving
   queue policy while deleting fixed handle tables.
5. **Land JSCU33's accessor correction, then JSCU34.** Both are contained
   carrier changes with direct conformance suites.
6. **Land JSCU32 and JSCU33's callable-code half.** These touch generated ABI
   and GC tracing, so they follow the ownership/lifecycle foundation and
   require the broadest forced-GC gates.
7. **Land JSCU35.** Compiler-only consolidation follows runtime correctness;
   it must preserve the D8.2.6 compiler-time ratios.

Structural ratchets:

- `sizeof(JsExecutionState)` ≤ **512 B** and no `JsRuntimeState` directory;
- `sizeof(NodeRuntimeSession)` ≤ **256 B**, excluding lazy capsule/resource
  allocations;
- zero direct subsystem-state pointers on `EvalContext` outside
  `ContextCapsuleDirectory`;
- zero `JsRootRange`, `JsRootedState`, `JsNamespaceState`, JS root reset
  registry and hand-written root-range count;
- an empty realm constructs only the execution, realm-slot and global-
  environment capsules; Node/Test262/readline/assert state is absent until
  first use;
- one context rid table; timer/RAF/Node/fetch code owns no fixed handle table;
- `LMD_TYPE_FUNC` allocations are actual callable values only;
- one environment GC tag, allocation path and tracer;
- no runtime `*_MAX` capacity unless the declaration cites the language,
  protocol, security or explicit nesting-policy bound it enforces;
- each retired cap has a focused test at old-limit + 1, including 1,025
  timers, 1,025 RAF jobs, 513 module-backed globals, 1,025 lexical bindings,
  257 async hooks, 257 ALS instances, 129 permission grants and 513
  diagnostics channels.

Every slice runs the §6 common gates plus its subsystem tests. JSCU27–JSCU34
add forced-GC and heap-replacement runs across at least ten realm epochs;
JSCU27/JSCU31 add two-context construct/destroy and Node attach/detach runs;
JSCU28/JSCU32/JSCU33 add MIR and AST call/generator/async/closure coverage;
JSCU34 adds resizable/detached ArrayBuffer, DataView, TypedArray and Node Buffer
coverage. Queue/call/compiler performance is measured only with `make release`
under rule 10. Each slice ends with the struct census, eager-allocation census
and capsule-construction census recorded beside the old/new values.

### 8.13 Implementation ledger (2026-09-09, in progress)

The following slices are implemented and retained as one coherent migration;
this ledger deliberately does not mark Phase 2 complete while its remaining
carrier and compiler work is open.

- **JSCU27:** `ContextCapsuleDirectory` now owns the prior direct
  `EvalContext` rendering, template, Node-session and JS-state pointers, and
  Node module state uses the capsule extension API. This is the context
  ownership boundary required by D5.4.2–D5.4.4.
- **JSCU28:** `JsCallActivation` owns a synchronous call's rooted ambient
  values, arguments and source/strictness metadata. The former physical
  pending-argument state fields are deleted; call nesting is activation
  linking, as required by D5.1.1v2 and D6.2.2v2.
- **JSCU28 supplement:** unordered `Set`/`Map` deep equality now uses one
  dynamic `JsUtilMatchLedger` for its candidate-consumption facts. The former
  1,024-entry native array could reuse a candidate after that index and report
  unequal collections as equal. Its two derived iterable arrays use the
  existing exact `RootVector` carrier while pair traversal holds the side-stack
  watermark, so recursive comparison cannot collect either array. A
  1,026-entry duplicate-candidate regression passes normally and with forced
  GC, per D5.1.1v2, D5.3.5 and D6.2.1.
- **JSCU29(A), partial:** `JsStringCacheState` now owns every realm-local
  concatenation, percent-escape, URI, ASCII and Test262 string cache in one
  contiguous precise root range. It replaces the duplicate
  `JsStringConcatState` and `JsGlobalStringCacheState` allocations and their
  separate root registrations; its byte/code-point tables remain finite cache
  domains, not multiplicity registries. The cross-path percent/URI/ASCII
  regression passes normally and with forced GC, per D5.3.5 and D5.4.2.
- **JSCU29(A), intrinsic-slot slice:** `JsRealmIntrinsicSlots` is now the
  one contiguous rooted owner for intrinsic method bindings, the prototype-key
  cache, generator-function prototypes, global/constructor and typed-array
  cache domains, and standard namespace objects. It replaces five
  same-lifetime root carriers and their independent registrations without
  reclassifying catalog-indexed constructor arrays as unbounded registries.
  Its checkpoint reset retains only intrinsic method identity, as before, and
  clears the other cache domains through the same owner. The
  typed-array base, base-prototype and per-brand cache builders now publish
  only through that range rather than adding duplicate per-slot registrations.
  The Math/constructor/Reflect/typed-array/generator cross-cache and explicit
  typed-array identity-after-collection regressions pass normally and with
  forced GC, per D5.3.5 and D6.2.2v2.
- **JSCU29(B):** `JsGlobalEnvironment` is one dynamic table of typed rows for
  lexical bindings, object-backed global `var` declarations and module-slot
  bridges. Its one `RootVector` owns row key/value storage and fixed realm
  globals; the former 1,024 lexical and 512 module binding capacities are
  deleted. This applies D5.3.5 without merging the three binding semantics.
- **JSCU29(A), active-module slot:** the replaceable active module namespace
  now uses the common `RootVector` carrier rather than a private `Item`, heap
  epoch and direct root-registration path. Its module-scope save/restore
  operation roots the incoming replacement across possible vector
  re-registration, so module evaluation keeps the same namespace identity
  through heap replacement. Existing CommonJS, eval-bridge and
  `vm.SourceTextModule` regressions pass normally and with forced GC, per
  D5.3.5 and D5.4.2.
- **JSCU29(A), module-provider slot store:** fifteen `vm`, public/internal
  async, internal utility/test, `module`, `cluster`, `repl`, and
  `internalBinding` namespace/binding caches now occupy the same
  `JsModuleRuntimeState::values` exact-root vector as the active module
  namespace. Their former function-static `Item`/epoch pairs and direct root
  registrations are deleted. A full batch clears providers just as the former
  epoch checks did, while a preamble checkpoint clears only the active
  namespace; `cluster.primary_options` separately derives `JsRootedState` and
  is retained only by that explicit checkpoint policy. The cross-provider
  regression, including an explicit collection, passes normally and with
  forced GC, per D5.3.5 and D6.2.2v2.
- **JSCU29(A), assert root range:** `JsAssertState` now derives
  `JsRootedState`; its public namespace, the two internal namespaces, and the
  two cached property keys are one contiguous five-Item range. The former five
  direct registrations and private key epoch are deleted, and internal-error
  construction roots its `codes` child across constructor installation. The
  assert pattern and `node:test` hook/mock regressions pass normally and with
  forced GC, per D5.3.5 and D5.4.2.
- **JSCU29(A), tagged-template registry:** native tagged-template site rows
  now carry only site metadata and a `RootVector` slot index; one dynamic
  vector owns every frozen template object. Reset and context teardown free
  metadata plus the one carrier, deleting the former per-entry root
  registrations. Same-site identity, raw content, and frozen-state survive an
  explicit collection normally and with forced GC, per D5.3.5 and D6.2.2v2.
- **JSCU29(A), await handoff:** `JsAsyncAwaitState` is the exact owner of the
  one resolved-value handoff that outlives `await`'s native suspension check.
  It replaces the private Item, heap-registration flag and epoch triple;
  suspended activations remain separate GC-owned frame carriers. A pending
  promise is explicitly collected, resolved with an object, and resumed under
  normal and forced-GC execution, per D5.1.1v2 and D5.3.5.
- **JSCU29(A), DNS range:** the five contiguous DNS namespace, resolver and
  server-cache Items now use `JsRootedState` and one exact realm range instead
  of five direct registrations plus a private epoch. The DNS namespace and
  promise-resolver regression passes normally and with forced GC, per D5.3.5.
- **JSCU29(A), crypto namespace:** `JsCryptoState`'s inherited exact realm
  range is the sole root owner of the lazy `crypto` namespace; its constructor
  no longer adds a duplicate direct registration. Requiring the namespace
  before and after collection preserves its identity normally and with forced
  GC, per D5.3.5 and D6.2.2v2.
- **JSCU30:** `RuntimeJob` and `RuntimeJobQueue` own nextTick, microtask,
  unhandled-rejection and RAF payloads. Timers carry the same job envelope,
  including an arbitrary Array argument pack, while their scheduling policy
  remains distinct under D6.3.1.
- **JSCU31 (staged):** one context-owned `RuntimeResourceTable` now owns the
  generation-checked rid, rooted script owner, typed immutable descriptor and
  native close callback for Node/Jube socket and crypto resources, JS timer
  handles, DNS lookup/resolve requests and their completion timers, and both
  built-in and dynamic `node:fs` async requests. Owner partitioning replaces
  the former parallel Jube-session and timer tables;
  `NodeRuntimeSession` and timer state retain only their lifecycle identity,
  while table rows retain the exact script-value span. Timer rows replace
  their private handle registry while leaving the timer-only `RuntimeJob` and
  libuv scheduling tail distinct. A timer row owns
  one exact root span for its public owner, callback, argument pack and
  async-context snapshot, replacing the former five direct timer-root
  registrations. The regression crosses the retired 1,024-handle limit,
  clears an interval and runs under forced GC. Mock-scheduler waits are likewise dynamic rooted records, with a
  regression past their former 128-row cap. Callback-style `fs` requests now
  likewise carry their callback, captured domain and pending initial error in
  a typed exact table span; native request tails retain only POD data and
  their rid. The Jube session token is cancellation and owner routing, not a
  second root protocol. Async read/write, `util.promisify(fs.read)`, and DNS
  lookup completion chains pass normally and under forced GC. The dynamic-module compatibility
  bridge translates its one legacy crypto prefix to a typed group before table
  teardown; no resource-table operation dispatches by a string prefix. The
  Node Blob-URL registry is likewise one dynamic `NodeBlobUrlEntry` table:
  each stable row joins its native URL ID and persistent Blob root, so create,
  resolve, revoke, reset and session detach share one ownership record instead
  of parallel 1,024-entry ID/value arrays. Its focused regression creates
  1,025 URLs, verifies lookup after explicit GC, verifies revoke, and runs in
  two successive Jube batch realms. Patched `net.Socket.prototype.connect`
  dispatch uses the shared exact `RootSpan` argument view rather than a private
  16-Item array, so every supplied argument crosses the native call boundary
  under forced GC (D6.2.1, D5.1.1v2). Other native owners remain to migrate.
- **JSCU31 follow-up, TLS continuation tail:** a `JsTlsSocket` now owns its
  pre-handshake write completions in one context-owned `RootVector`, replacing
  the private `PendingTlsWriteCallback` linked-node type. The FIFO head is
  POD metadata; the vector is the sole owner of callback Items, compacts only
  between completion passes, and is destroyed with the socket on every native
  close path. The focused two-write regression passes in the compiled suite
  normally and with forced GC, per D5.1.1v2 and D5.4.2. TLS socket handles
  themselves remain a later `RuntimeResourceTable` migration.
- **JSCU31 follow-up, out-of-order callback continuation tail:**
  `RuntimeCallbackSlots` is the single context-owned carrier for native
  operations whose completions can arrive out of submission order. Its sole
  `RootVector` owns callable `Item`s, while each libuv request retains only a
  stable POD slot; completion takes its slot and shrinks only completed tail
  slots, never moving a live request's index. `JsProcessState` uses that
  carrier for `process.send`, each `JsHttpClientReq` for streamed
  `ClientRequest.write`, and each `JsSocket` for its pre-connect byte queue,
  submitted writes and delayed `end()` shutdown completion. The socket's
  byte-list is still its transport-specific FIFO tail; no second callback
  owner parallels it. Parent/child IPC, two-write HTTP client, and two-write
  pre-connect socket-plus-end regressions pass in the compiled suite normally
  and with forced GC, per D5.1.1v2 and D5.4.2. This deliberately remains
  distinct from TLS's FIFO pre-handshake queue: completion order is part of
  the latter's semantics.
- **JSCU31 follow-up, HTTP response write tail:** `HttpResponseWriteReq` now
  contains only the native completion data it actually consumes (connection,
  byte count and close policy). Its former raw response and callback Items
  were redundant—the response end callback is part of the initiating
  activation and runs when the write is accepted—so that activation retains
  the response and callback in one exact root frame instead. The response
  body/end-callback regression passes normally and with forced GC, per
  D5.1.1v2 and D5.4.2.
- **Root-storage capacity follow-up:** AsyncLocalStorage and assertion
  instances use context-owned `RootVector`s; readline input/interface pairs
  are dynamic `JsReadlineInput` rows with one rooted value store. Their former
  capped registries and the readline input-map range registration are deleted.
  `process` now has one dynamic event-listener map for lifecycle and ordinary
  events; its former 32-entry `exit`/`uncaughtException` shadow lists and
  duplicated root span are deleted. `async_hooks` likewise owns dynamic hook
  and deferred-destroy RootVectors, deleting its separate 256-hook and
  1,024-destroy capacities and count metadata; the regression crosses both
  limits. Test262 agent callbacks and reports now use a dynamic callback store
  and `JsTest262AgentReport` rows, rather than fixed callback/report/waiter
  arrays; Atomics’ per-agent waiter metadata follows the same agent-slot
  identity rather than a second 16-entry table, and each Atomics waiter row
  owns its rooted promise slot rather than a 128-entry promise array. The
  regression crosses every former agent/report/waiter bound under regular and
  forced-GC execution. This applies D5.1.1v2 and D5.3.5 to each one
  outliving-instance collection, without claiming the broader JSCU29(A)
  realm-slot migration is complete. The `with` scope chain now uses its
  existing dynamic `JsItemStack` without a 16-depth guard, and the call
  boundary snapshots an arbitrary-depth chain in one temporary exact root
  range rather than a fixed native array; a 17-deep closure call survives
  forced GC. Eval source context likewise uses dynamic `JsEvalSourceRecord`
  rows with paired RootVector slots for filename/source instead of separate
  16-entry Item and metadata arrays; a 17-deep nested eval passes under
  forced GC. Each eval bridge now uses one dynamic `JsEvalBindingJournal` for
  frame marks, binding facts and its paired RootVector lanes; caller-local
  state likewise uses dynamic `JsEvalLocalFrameMarks` rows plus RootVector
  lanes for `var`, lexical and immutable facts. The former 512-binding and
  32/64-frame bridge limits are gone, as is the private-name journal's
  256-binding limit. A direct eval exporting 513 vars, then resolving the
  last one through a second direct eval, and a 257-member private class eval
  both pass under forced GC. DOM Web Storage entries are dynamic `JsDomStorageEntry` rows in
  their lazy platform capsule rather than a 128-entry key/value table; the
  regression writes, reads, removes and clears 129 entries. Media-query
  wrappers use dynamic `JsDomMediaQueryState` records with one RootVector for
  their wrapper objects instead of a 64-entry table and fixed root scan; the
  regression creates 65 queries. The headless dialog FIFO uses dynamic
  `DomPromptResponse` rows, keeping ordered text and Cancel records together
  without a 32-slot ring. The Node CommonJS module chain now also
  uses its existing dynamic `JsItemStack` without a second 128-depth policy
  guard; allocation failure is reported rather than silently changing the
  parent chain. A focused 161-module temporary fixture verifies every
  `module.parent` link and deletes its files after the run. Node `net.BlockList`
  now owns dynamic rule and instance collections instead of separate 128-rule
  and 256-instance native arrays; the 129th address remains observable in its
  focused regression. The Node forced-GC module path currently exits without
  script output, so this BlockList slice claims normal-runtime coverage only.
- **JSCU29(A) registry step:** the remaining `JsRootRange` reset registry and
  its per-range callback protocol are deleted. One fixed-realm catalog now
  configures and clears the same owned ranges, so no second dynamic collection
  can drift from the storage owners. Preamble reuse passes builtin-cache
  retention explicitly to the lifecycle reset instead of encoding that policy
  as a reset-engine exemption; dynamically allocated Node extension records
  still unregister their exact range before freeing native storage. The `with`
  binding memo remains on the shared `RootVector` mechanism. The range
  mechanism itself remains pending replacement by the realm-slot catalog;
  this step removes its duplicate reset authority per D5.3.5 and D6.2.2v2.
- **JSCU29(C):** `JsNodeTestHookLedger` is the one rooted, growable execution
  record for `node:test` `beforeEach` and `afterEach` hooks. It removes the
  duplicate guest-array mirrors, fixed 64-entry native arrays and independent
  counters; scope/run marks now shrink each phase lane directly. `afterEach`
  registrations are retained for the test completion phase rather than being
  spuriously invoked during registration. The focused regression crosses the
  former limit in both lanes, per D5.3.5 and D6.3.1.
- **JSCU29(D):** `JsAssertMockRegistry` owns all `node:test` mock rows and
  their rooted calls/original pair. One payload-addressed native wrapper now
  replaces 32 generated slot wrappers, a 64-entry backing array and the
  untracked fallback; `.mock.callCount()` resolves its dynamic row id through
  the same registry. The focused regression creates and invokes mock 65, per
  D5.3.5 and D6.2.1.
- **JSCU29(E):** `JsConsoleLabel` is the one dynamic realm record for both
  `console.count` and `console.time` state. The separate 64-count and
  32-timer hash arrays, fallback-to-label-zero behavior and hash-only aliasing
  are deleted; a label is copied and compared exactly once. The focused
  regression creates 65 labels and confirms that the first count remains
  independent, per D5.3.5 and D6.3.1.
- **JSCU29(F):** the `node:test` namespace and transient run-event queue are
  slots in one `RootVector`, removing two unregistered `Item` fields and the
  queue-only `roots_epoch` mechanism. The forced-GC probe exposed a separate
  root-lifetime defect in `Object.freeze`: its reflected own-key Array was
  traversed from an unrooted local while descriptor updates could collect. The
  integrity walk now owns both object and key Array precisely. The probe and
  the 65-hook/65-mock regressions run with `LAMBDA_GC_FORCE_EVERY=1`, per
  D5.1.1v2, D5.4.2 and D5.4.4.
- **JSCU29(G):** `JsAssertPatternKeyList` is the one rooted, growable
  expected-object key carrier for `assert.throws` validation and mismatch
  rendering. Its dynamic comparison flags replace both 128-key stack scratch
  arrays, so no expected property is silently omitted once collection or
  diagnostic rendering crosses that former ceiling. The 129-key regression
  accepts an exact pattern and rejects a mismatch in key 129, per D5.3.5 and
  D5.4.2.
- **JSCU32:** `GC_TYPE_ENVIRONMENT` is the one environment allocation/tracer
  family. Its header descriptor distinguishes raw closure Item slots from the
  interpreter lexical payload while retaining the same scalar-tail and
  precise-trace contract. `JsSuspendedActivation` now owns the common durable
  generator/async activation edges and replay ledger; their state-machine
  tails remain separate as D6.2.2v2 and D8.1.3v10 require. Interpreted async
  loop continuations now rely on that carrier's exact trace callback for their
  iterator, `for...in` object and lexical environment, deleting their second
  per-continuation direct-root protocol. A suspended `for await` iterator
  resumes with an object after explicit collection in normal and forced-GC
  CLI execution, per D5.1.1v2 and D5.1.3.
- **JSCU33(B) and JSCU34:** accessors use a dedicated traced
  `JsAccessorCell`, not a fake function layout; its synthetic storage tag is
  now recognized by the collector as that cell's exact pointer encoding, so a
  rooted property map traces the getter/setter edges rather than reclaiming a
  live accessor after collection. `JsArrayBufferView` is the shared
  typed-array/DataView buffer-view header. These restore D2.4.1's single
  carrier authority.
- **JSCU35(1):** property-Item and module-NameId lowering caches are now
  instances of the one `JsMirNameCache` key/owner/count storage record. Their
  result domains remain explicit, preserving D8.2.4's compiler semantic
  boundary while removing a duplicated cache mechanism.
- **JSCU35(2):** `JsMirCursor` is the checkpointable function/class/scope
  lowering position. Conditional branch transactions now save and restore
  that cursor once, then add only their lexical-map transaction and
  closure-journal mark as D8.2.4 requires.
- **JSCU35(3):** `JsClassMember` is the one source-ordered tagged compiler
  record for methods, static fields, instance fields and static blocks.
  Class lowering filters that table for its semantic pass rather than
  reconstructing order from parallel arrays; the focused regression covers
  computed keys, static initialization, private fields and inherited methods
  under normal and forced-GC execution, per D8.2.4–D8.2.6.
- **JSCU35(4a):** `JsMirBindingRef` is the shared source-binding fact record
  inherited by module-constant and temporary name-set rows. It owns the
  spelling, resolver identity, defining node and declaration kind once;
  module-cell and analysis-range tails remain distinct. This is the first
  safe reduction of the binding-fact duplication described by D8.2.4; emitted
  artifact ownership remains open.
- **JSCU35(4b):** Dynamic Function compiled-artifact lookup now owns one
  growable collection of stable `JsDynFuncCacheEntry` rows. The code store
  remains the source/MIR payload owner, while each cache row owns only its
  lookup facts; the former 256-entry fixed array and overflow fallback are
  deleted. The regression compiles and invokes 257 distinct functions, then
  repeats under forced GC, per D8.2.4.
- **JSCU35(4c):** MIR signal-recovery ownership now uses dynamic
  `ActiveJsTranspileOwner` rows, rather than a second fixed 32-entry compiler
  stack. The recovery capsule still owns only in-flight transpiler/source
  cleanup; completed code remains owned by the code store. Deep CommonJS
  compilation exercises the growable stack and batch teardown, as D8.2.4's
  compiler ownership boundary requires.
- **JSCU35(4d):** completed-unit ownership is one dynamic
  `JsCompiledArtifact` row store: each stable row joins the MIR context with
  its retained source buffer, and batch teardown disposes that row once after
  MIR cleanup. The former raw artifact array plus independent count/capacity
  mechanism is gone; Dynamic Function and deep CommonJS regressions retain
  their normal and forced-GC coverage under D8.2.4.
- **JSCU35(5):** RegExp frontend validation and named-backreference rewriting
  use one dynamic `JsRegExpNameList` mechanism for borrowed named-capture and
  backreference facts. The separate 96-entry group/backreference arrays no
  longer silently stop duplicate/reference validation; a duplicate name after
  97 captures is rejected, while an ordinary named backreference remains
  accepted under forced GC. RE2 name adaptation uses the matching one-row
  `JsRegexNameAlias` payload and a growable compiler list, replacing a second
  four-array 96-alias mechanism; the 97th ECMAScript-valid `$` capture name is
  preserved in `groups`. Legacy last-match state likewise has one dynamic
  rooted `JsRegexpLastMatch` value carrier instead of a nine-capture native
  array, so `$+` remains the final capture after a 12-group match under forced
  GC, per D8.2.4 and D5.3.5.
- **JSCU35(6):** `EarlyErrorNameLedger` is the one dynamic compiler-pass
  carrier for private declarations and break/continue label facts. Its tagged
  source-name rows encode label kind and function boundary, replacing three
  fixed parallel name/length arrays (128 private names, 32 iteration labels
  and 64 labels). The regression resolves private name 129 and a 65th
  iteration label under normal and forced-GC execution, per D8.2.4 and
  D5.3.5.
- **JSCU35(7):** switch lowering owns one dynamic `JsMirSwitchCaseRow` for
  each source case and its emitted label, so the former paired 128-case
  scratch arrays cannot omit a late branch. Class static inheritance no
  longer copies ancestor methods through a second lowering mechanism: the
  class constructor's existing `[[Prototype]]` superclass link is the sole
  authority. The prototype-mutation boundary retains its visited identities
  in a dynamic rooted path to reject cycles, leaving the shared property walk
  unbounded and allocation-free rather than governed by a 32-level cutoff.
  `using` disposal owns its separate growable ordered resource rows because
  reverse disposal order is its distinct semantic tail. The 129th switch case
  and a 33-level static class chain (including its observable constructor
  prototype link) execute under normal and forced-GC execution, per
  D8.2.4–D8.2.6 and D5.3.5.
- **JSCU35(8):** module-variable lowering no longer imposes a separate
  16,384-slot JS ceiling. It allocates indexes directly into the existing
  growable `LambdaModuleState` slab, whose prepare/grow path owns the precise
  root-range replacement; bulk initialization now rejects only invalid
  negative indexes. The existing direct-eval 513-binding regression continues
  to exercise the dynamic slab, per D8.2.4 and D5.3.5.
- **JSCU35(9):** `JsVmTemporaryBindingJournal` is the one dynamic rooted
  owner for transient `vm` context aliases and `vm.compileFunction`
  `contextExtensions` keys. It replaces the separate unrooted 16-key scratch
  arrays and removes the silent extension truncation; teardown deletes its
  recorded own keys in reverse insertion order. The 17th context extension is
  visible to the compiled function under normal and forced-GC execution, per
  D8.2.4 and D5.3.5.
- **JSCU35(10):** `RootSpan` is the one activation-local ABI carrier for all
  `diagnostics_channel` trace arguments. Its `JsDcRestArguments` view replaces
  the separate 15/16-Item scratch arrays in trace, callback and bounded-channel
  paths, eliminating silent argument truncation while retaining exact roots
  across callbacks. The 17th trace argument reaches its target under normal
  and forced-GC execution, per D6.2.1 and D5.1.1v2.
- **JSCU35(11):** a decorated declaration's reduction-owned dynamic AST-child
  list is the sole compiler carrier for its ordered decorator expressions.
  Lowering now consumes that list directly instead of copying it through a
  separate 16-pointer parser scratch array. A 17-decorator class preserves
  every application and its class value under normal and forced-GC execution,
  per D8.2.4 and D5.3.5.
- **JSCU35(12):** `JsVmStmBindingList` is the sole temporary binding carrier
  while `vm.SourceTextModule` rewrites imported local names. Its rows own exact
  local/export spellings and dependency identity, replacing a separate
  32-entry stack table whose 33rd alias was silently omitted. The focused
  module regression observes alias 33 under normal and forced-GC execution,
  per D8.2.4 and D5.3.5.
- **JSCU35(13):** repeated-RegExp capture cleanup now sizes its group-fact and
  nesting scratch from the compiled pattern, using the one growable
  `JsRegexScratch` mechanism instead of paired 256-entry arrays. A child
  capture after index 256 is cleared when a later quantified iteration omits
  it, preserving the observable `undefined` result under normal and forced-GC
  execution, per D8.2.4 and D5.3.5.
- **JSCU35(14):** `JsRegexFilterList` is the compiled wrapper's sole owned
  post-filter sequence. Dynamic assertion and synthetic-marker analysis feed
  that list, and its rows release their nested regular expressions exactly
  once. The former 16-filter array and its matching rewrite scratch arrays are
  gone; a 17-negative-lookahead expression compiles and evaluates normally
  and under forced GC, per D8.2.4 and D5.3.5.
- **JSCU35(15):** regex backtracking selection now uses dynamic `JsRegexScratch`
  carriers for group boundaries and optional/unbounded quantifier facts instead
  of three 64-entry arrays. A lookahead nested beneath 65 ordinary groups
  preserves its enclosing-pattern semantics under normal and forced-GC
  execution, per D8.2.4 and D5.3.5.

Open: JSCU29(A)'s full realm-slot catalog and deletion of the remaining
root-range mechanism, JSCU31's remaining native-owner migration to the resource table,
JSCU33(A)'s immutable callable-code owner, JSCU35's remaining emitted artifact
consolidation, the lazy realm-capsule census and all final release performance
gates.

---

## Appendix A — Implementation notes (brief)

- **Item 1 carrier form.** Keep `Map base` first so every existing
  property-face path is unchanged; the payload follows. If `sizeof` pushes
  the carrier past its object-zone size class, the AST-interpreter records
  (the six `ast_*` fields and the two continuation pointers) move to one
  optional `JsGeneratorAstState*` allocated only for interpreted
  generators, which also removes them from every MIR generator.
- **Item 1 handler binding.** The resume/reject reactions are ordinary
  `JsFunction` values whose bound argument is the frame Item; no new
  callable kind is needed, and the frame is reachable through the reaction
  exactly as any bound argument is. Under JSCU25 the reaction body is
  "resume my task one step": it calls the task's resume entry directly, so
  the scheduler's run queue is never consulted for a JS continuation.
- **Item 1 task creation.** Add a `lambda_task_create_weak(scheduler,
  resume, frame, destroy, handle_item)` beside `lambda_task_create`; the
  existing function keeps its strong semantics for `start`. The mailbox
  becomes lazy in both: `mailbox.items` is allocated and root-registered on
  the first `send`/`recv`, which also removes one root range from every
  Lambda task that never uses its mailbox.
- **Item 2 block size.** 64 Items per block keeps the first block at 512 B
  and the registration count per vector small; the eval journals and the
  global-binding table are the only consumers expected to exceed one block
  in ordinary runs. Record the high-water mark in the census test.
- **Item 3 migration.** Register IDs and ops for the four existing
  `EvalContext` capsules first with no behaviour change; then the thirteen
  JS `void*` slots; then the embedded records one at a time, ratcheting
  `sizeof(JsRuntimeState)` after each. Keep `js_runtime_state.<field>`
  macros as accessor aliases until their last reader is converted.
- **Item 4 migration.** Add `code` beside the existing fields with a
  compatibility accessor per moved fact; convert factories, finalize, both
  tracers, the call/new kernels and builtin installation; delete the
  fields under a static-assert on the new sizes. `Function` and `JsFunction`
  can move in separate commits as long as the tracer arm handles both
  layouts by `layout_kind` from the first commit.

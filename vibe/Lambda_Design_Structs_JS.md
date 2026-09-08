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

- **JSCUO1 — ratified 2026-09-07 as JSCU25** (item 1).
- **JSCUO2 — ratified 2026-09-07 as JSCU26** (item 1); D5.1.1→v2 in
  spec 1.51.0, SF20 addendum in `Lambda_Design_Stack_Frame.md`.
- **JSCUO3 — Who constructs DOM capsules when no JS realm exists.** JSCU17
  says "whichever language first touches them"; the observer delivery path
  and XHR turn tokens today assume `js_active_runtime_state`. The Lambda
  DOM package (`lambda/package/dom`) is the test case: a Lambda page with a
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
  (`dom_xhr.cpp:67`). Parent §6.3, §6.4, §7.2 apply unchanged; D7.4.5's
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

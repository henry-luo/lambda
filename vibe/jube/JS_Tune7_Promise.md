# JS Tune7 — GC-Owned Promises and JS-First Unified Async Kernel Plan

**Date**: 2026-08-13

**Status**: IN PROGRESS — the GC carrier, growable reactions, shared deque,
and JS-first queue lanes are landed; native completion subscriptions and the
R6b Lambda scheduler handoff remain

**Implementation anchor**: current worktree after JS Tune6 completed the JR4
object-metadata and exotic-operation migration. Tune6's exact
`__promise_idx` exception is the handoff boundary; no other sentinel protocol
may be introduced.

**Design authority**:
[`JS_Runtime_Redesign.md`](JS_Runtime_Redesign.md), especially **JR1**,
**JR7**, and **JR7.1**. Governing formal rulings are **D1.2–D1.3**,
**D2.1.2**, **D2.1.4–D2.1.6**, **D4.1.1**, **D4.3.1–D4.3.3**,
**D5.1.3**, **D5.2.2**, **D5.3.1–D5.3.5**, **D5.4.1–D5.4.4**,
**D6.1.2**, **D6.3.1**, **D7.4.1v2**, and **D8.4.3**. The Promise-carrier
clarification was adopted in formal-design version 1.16.0; §3.2 records its
boundary.

This document is the execution plan for redesign phase R6a/JR7. It replaces
the fixed Promise record slab, Map wrapper, index sentinel, fixed reaction
arrays, fixed Promise/microtask queues, and their root-registration machinery.
It also establishes the core-owned async deque and native completion-
subscription contracts, then migrates the JS side first. The Lambda task
scheduler/state machine remains unchanged until R6b; Tune7 must leave it a
clean Item-first compatibility boundary, not pull Lambda concurrency redesign
into the Promise carrier cutover.
It preserves the existing Promise Resolution Procedure, species behavior,
thenable assimilation, async/await lowering, event-loop ordering, Node async
hooks/domains/AsyncLocalStorage behavior, and Lambda-to-JS Promise membrane.

Per **D1.6**, compiler/runtime work is MIR Direct only. C2MIR, vendored
dependencies, parser/grammar work, JR8 feedback vectors, JR9 modules, and the
JR10 broad `js_runtime.cpp` split are not Tune7 work.

### Implementation progress (2026-08-13)

The current worktree has landed the JS-first portion of R6a: native Promises
are GC-owned VMaps with vtable branding and traced Item edges; reactions use a
growable GC Array; nextTick, Promise microtasks, and unhandled-rejection
candidates use the core `RuntimeAsyncDeque`; and async-context Promise roots
survive forced collection. Growth fixtures cover 1,100 microtasks, 1,100
unhandled candidates, and 32 reactions, with poison-GC coverage for Promise,
finally, thenable, combinator, and async/await paths. This implements the JS
side of **JR7.1** while preserving the distinct Lambda task state machine.

Still open are the native completion-subscription API, the full structural
census/LOC exit, release-performance evidence, and the R6b Lambda-ready lane
and observer/closure migration. Those remain explicit follow-up work rather
than being counted as completed by this JS-first landing.

---

## 1. Outcome and non-negotiable exits

Tune7 is complete only when a native Promise is one ordinary GC-managed
`Item`, its internal edges are precisely traced, reaction/job storage grows
without semantic caps, and every JS Promise/job predecessor mechanism is
deleted. The R6b Lambda-side bridge/scheduler predecessors are named
separately and are not falsely counted as Tune7 deletions.

| Gate | Required final result | Evidence |
|---|---|---|
| Promise identity | A native Promise is one `LMD_TYPE_VMAP` heap object branded by the immutable Promise vtable. Its `Item` points directly to that object. | Allocation/layout tests; identity tests; no wrapper conversion. |
| Lifetime | Each Promise is independently collectible. No Promise record is retained merely because it was allocated earlier in the context. | Forced-GC liveness/finalization tests and live/peak diagnostics. |
| Representation count | Exactly one native Promise representation exists. | Mechanism census: Promise representations `2 -> 1`. |
| Reaction storage | One growable reaction vector stores typed reaction records. No `[8]` arrays or `then_count < 8` branch remains. | More-than-eight reaction/adoption tests and source ratchet. |
| Property behavior | Internal Promise slots are never own properties. User expandos, descriptors, Symbols, custom/null prototypes, and extensibility use Tune5/Tune6 property semantics. | Reflection/prototype/descriptor matrix. |
| Brand | Promise classification uses vtable identity through `js_object_meta`/`js_promise_is`, never `host_type`, a name, a property, or `map_kind`. | Resolver census and spoofing tests. |
| Rooting | Promise allocation registers no root range. VMap tracing marks only the live Promise's Item fields and reaction/expando owners. | GC callback tests and root-registration profile. |
| Async ownership | Resolver closures, async contexts, TLA/module fields, queued jobs, and host bridges retain Promise `Item`s, never integer table indices or immortal raw pointers. | Index census and forced-GC suspension tests. |
| Shared async base | The growable Item deque and one-shot completion-subscription mechanism are runtime-owned and JS-neutral. No core header depends on `JsPromise`, a JS closure, or ECMAScript policy. | Dependency/census check and non-JS substrate unit tests. |
| Jobs | Promise jobs and `queueMicrotask` share one growable microtask lane. `process.nextTick` keeps its distinct priority lane but uses the same core deque implementation; the Lambda-ready lane is reserved but not migrated in R6a. | Ordering, growth, shutdown, reset, and lane-isolation tests. |
| Promise completion seam | Promise settlement can publish to rooted native subscribers without synthesizing JS `.then` closures. Publication names a destination lane and never invokes a foreign continuation inline. | Native subscribe/late-subscribe/GC/ordering tests. |
| Queue failure | Capacity is memory-bounded, not constant-bounded. Growth failure follows the in-band error/failure policy; no job or reaction is logged and silently dropped. | Fault-injection tests and zero overflow-drop branches. |
| Unhandled rejection | One short-lived strong candidate queue is drained after the microtask checkpoint. There is no Promise-table scan or weak side table. | Suppression/report/late-handler tests and queue census. |
| Sentinel deletion | `__promise_idx` and every index conversion/helper are absent from production and tests. | Zero-reference ratchet. |
| Fixed-cap deletion | `JS_PROMISE_STATE_MAX`, `JS_PROMISE_UNHANDLED_QUEUE_MAX`, and Promise/microtask fixed-cap overflow paths are absent. | Zero-reference ratchet. |
| Reset/teardown | Heap replacement, batch reset, context destroy, and event-loop shutdown clear only context roots/counters; GC owns Promise reclamation. | Multi-epoch batch and teardown tests. |
| Behavior | Focused JS, Test262 Promise/async, Node Promise/microtask, MIR/GC, Jube concurrency, and Radiant async gates are green. | T7 validation transcript. |
| Performance | Release builds show no unexplained Promise/job regression; Promise root registration is absent and its profile bucket remains at noise. | Interleaved release A/B plus release-profile attribution. |
| Source size | Production C/C++ is net-negative for the phase after predecessor deletion. | Clean T0/T7 LOC accounting. |

An adapter is not an exit for the Promise carrier. A VMap that still indexes a record table, a GC
object held forever by a context list, a growable list beside the fixed arrays,
or a job deque beside the fixed rings fails Tune7.
The existing Promise/task bridge is the one explicit R6a boundary: it may
remain only because R6b owns the Lambda-side switch, and it must consume the
final Item-first Promise API without restoring table/index ownership.

---

## 2. Why the redesign is better

### 2.1 Direct comparison

The current table is not a literal C++ `static JsPromise[8192]`. It is a
per-runtime, lazily allocated, fixed-capacity slab. "Static table" below means
fixed-index external storage whose records live until reset/destroy.

| Current design | Proposed JR7 design |
|---|---|
| One fixed 8,192-record slab | One GC object per live promise |
| Records survive until context reset | Unreachable promises are collected individually |
| Maximum 8 pending reactions | Growable reaction vector |
| Promise record plus wrapper Map | One VMap-backed promise value |
| Wrapper finds record through `__promise_idx` | Item directly identifies the promise |
| Seven root ranges registered per used slot | VMap tracer visits the promise's actual live fields |
| Hard 8,192-promise limit | Limited by available heap memory |
| Fixed 1,024-entry microtask arrays | Growable job deque with no capacity-drop path |

The original JR7 evidence measured a 7 x 8,192 registration storm. Tune1-P1a
has since made registration incremental, so current steady-state allocation
registers seven ranges per newly used record rather than all 8,192 at once.
The architectural problem remains: the used-slot high-water mark only grows,
all used records stay roots, and a new heap epoch replays registrations up to
that high-water mark. T0 must remeasure the current cost instead of copying the
old 7.1% result into Tune7's acceptance record.

### 2.2 Improvements summary

1. **It fixes real semantic limits.** A pending Promise currently accepts only
   eight reactions. Once `then_count == 8`, later `.then()` or adoption
   reactions are skipped. A growable vector makes reaction registration
   memory-bounded rather than silently wrong.

2. **Dead Promises can actually die.** Current records, results, handlers, and
   wrappers remain rooted until reset. A normal heap object becomes collectible
   as soon as JavaScript, the job queue, async state, and host state stop
   referencing it, applying **D4.3.1–D4.3.3** and **D5.3** directly.

3. **It eliminates double representation.** Native code currently uses a
   `JsPromise*`, JavaScript sees a Map wrapper, and `__promise_idx` connects
   them. Tune7 makes the VMap-backed native object itself the JavaScript value,
   removing wrapper construction, hidden-index lookup, and identity
   synchronization.

4. **It removes per-Promise root registration.** The current path registers
   seven root ranges for each used table slot and replays the used prefix after
   heap replacement. Tune7 uses the VMap's allocation-free trace callback;
   context queues root one stable owner `Item`, not every entry.

5. **It models ECMAScript Promise internals correctly.** Promise state and
   reactions are internal slots, not ordinary own properties. The Promise ops
   table exposes only user-created expandos and the selected prototype; no
   internal spelling can collide with reflection or user data.

6. **It follows Lambda's existing architecture.** The carrier applies
   **D1.2** (one `Item` currency), **D1.3** (one GC/runtime), **D2.1.4**
   (layered container refinement), **D2.1.6** (lifecycle designed with the
   representation), and **D4.3.3** (precise VMap tracing/finalization), rather
   than maintaining a JS-private lifetime table.

7. **It creates the right Promise/task interop seam.** Promise and Lambda task
   state machines keep their own semantics, but share runtime-owned queue,
   rooting, readiness, and completion-subscription primitives. R6a makes
   Promise a native completion publisher and moves JS scheduling onto the
   shared deque; R6b can then replace the rooted task observer and the two
   synthetic `.then` closures with direct subscriptions. This preserves the
   **D6.3.1** microtask/macrotask boundary while removing interop-only objects.

Expected tradeoffs are explicit. Cold instance-property operations gain a
Promise vtable/ops dispatch; a Promise with many reactions grows storage; and
GC now visits live Promise edges rather than scanning a permanent table root.
Those are the intended costs. Promise methods, `await`, resolution, and job
drain remain direct struct/array operations and are the hot path. Release
measurements, not architectural preference, decide whether any implementation
detail needs further tuning.

---

## 3. Preconditions, scope, and invariants

### 3.1 Tune6 handoff prerequisite

P1 cannot start until Tune6's completed handoff is reproducible:

1. `TypeMap::js_meta` and `JsPropertyOps` are the only JS class/exotic
   metadata mechanisms under **D3.4.7**;
2. `JS_CLASS_PROMISE` has stable intrinsic metadata;
3. the only remaining engine sentinel is the exact JR7-owned
   `__promise_idx` allowlist;
4. VMap/host property operations already enter the Tune5 eight-operation
   property core;
5. no semantic `map_kind`, fake TypeMap, or `Map.data` native payload is
   needed for Promise migration; and
6. Tune6's focused JS, GC, Node/Jube, and release gates are green.

If the handoff is dirty, T0 records the exact delta and stops. Tune7 does not
recreate a compatibility classifier around incomplete Tune6 work.

### 3.2 Adopted formal authority: `D7.4.1v2`

Formal-design version 1.16.0 revised **D7.4.1** in place as
**D7.4.1v2**. VMap is now selected by native/internal-slot representation,
not module ownership, so an engine-owned Promise may use one immutable VMap
brand with precise vtable trace/finalize hooks. Shape-backed ECMAScript
objects remain typed Maps; raw native pointers remain non-visible; and no new
TypeId or lifetime mechanism is introduced. JR7 and §§5.1–5.7 own the detailed
Promise carrier, property, and queue contracts.

No Lambda-language semantic ruling changes, so
`doc/Lambda_Formal_Semantics.md` does not need a Promise representation edit.

### 3.3 In scope

- Make VMap GC trace/destroy dispatch per vtable, fulfilling **D4.3.3** for
  more than the HashMap backend.
- Define the final GC-owned `JsPromise` VMap carrier and immutable vtable
  brand.
- Convert Promise algorithms from index/raw-pointer ownership to rooted
  Promise `Item` ownership.
- Replace fixed reaction arrays with one growable, precisely traced reaction
  vector.
- Add lazy ordinary expando storage and Promise-owned prototype/extensibility
  state through the existing `JsPropertyOps` surface.
- Delete the record slab, wrapper Map, `__promise_idx`, per-record roots,
  capacity logs, and reset/free paths.
- Convert `JsAsyncContextStateRecord::promise_idx` and resolving-state `idx`
  to Promise Items.
- Preserve and verify Promise use by async functions/generators, TLA/dynamic
  import, Lambda concurrency, Node APIs, Web APIs, async hooks, domains, and
  AsyncLocalStorage.
- Add a core-owned, JS-neutral runtime async module containing the growable
  rooted Item deque, lane/checkpoint vocabulary, and one-shot native
  completion-subscription primitive required by JR7.1.
- Replace fixed nextTick/microtask ring storage with separate JS lane instances
  of that shared deque while preserving priority and checkpoint ordering.
- Publish Promise settlement through the native completion seam after Promise
  state is committed; support rooted native subscribers and late subscription
  without running foreign continuations inline.
- Replace the fixed unhandled-rejection array with the chosen strong
  checkpoint queue.
- Add structural census, behavior, forced-GC, reset, and release-performance
  evidence.
- Update JS async/runtime documentation after the implementation is final.

### 3.4 Out of scope

- Changing ECMAScript Promise ordering, species, thenable, combinator,
  `finally`, async-hook, domain, or rejection-event semantics except where the
  existing fixed caps demonstrably violate them.
- Replacing the async/generator state-machine lowering, the 256 async-context
  cap, TLA approximation, timer/RAF storage, or libuv.
- Migrating `LambdaTask`, its MIR continuation/frame, mailbox, cancellation,
  join/wait state, or scheduler run queue onto the shared kernel. That is R6b
  after the JS producer/queue contracts are proven.
- Deleting or rewriting the existing Promise/task interop bridge in R6a. It
  remains a compatibility client of the final Promise Item API; R6b replaces
  its rooted observer and synthetic fulfillment/rejection closures with native
  subscriptions.
- Merging JS Promise and Lambda task state, error policy, cancellation, or
  scheduling priority. **D6.1.2**, **D6.3.1**, and **S13.1.2–S13.1.5** keep
  these semantic owners distinct.
- A second Promise-only microtask queue beside the event loop queue.
- Weak references, ephemerons, a new Promise TypeId, pointer tagging, handle
  tables, conservative native-stack scanning, or a process-global Promise
  registry.
- JR8 feedback vectors/IC policy, JR9 module registry work, or broad JR10 file
  decomposition.
- Parser, grammar, C2MIR, vendor, unrelated Lambda core, Radiant layout, or DOM
  redesign.
- Treating performance counters, debug names, or inspector strings as semantic
  Promise classification.

### 3.5 Invariants carried through every phase

1. `get_type_id(Item)` remains the representation entry under **D2.1.2**; a
   Promise is `LMD_TYPE_VMAP`, with Promise semantics refined by immutable
   vtable identity.
2. `js_promise_is`/`js_object_meta` are the only Promise brand boundary. No
   string, property, `host_type`, index range, or diagnostic path may classify
   a Promise.
3. A native Promise has exactly one script-visible identity. Converting between
   `Item` and `JsPromise*` allocates nothing and is pointer identity only.
4. Every `JsPromise*` is a short-lived derived pointer from a rooted Promise
   Item. It never crosses `MAY_GC`, callback/user re-entry, queue drain, or
   libuv polling without that owner root under **D5.3.2–D5.3.3**.
5. Promise settlement is one-way. Result publication, state change, reaction
   detachment, async-hook notification, and unhandled scheduling have one
   owner and a documented order.
6. Every outliving arbitrary Item store uses destination-owned scalar storage
   under **D5.2.2**. The shared internal item-vector helper must use Lambda
   Array storage/`owned_item_slot_store`, not borrow a number-frame address.
7. VMap trace/finalize callbacks allocate nothing, run no script, acquire no
   locks, and are idempotent at heap sweep/context teardown under **D4.3.3**.
8. Promise internal state is never enumerable, configurable, writable through
   reflection, serializable, or addressable by a user key.
9. Lazy expando creation happens only on the first successful own-property
   mutation. Read/has/delete/keys/descriptor misses allocate no expando.
10. Promise prototype identity is resolved through rooted realm intrinsic
    state; no realm Item is stored in static metadata under **D5.4.3**.
11. The queue owns every enqueued callback/resource/ALS/domain Item before the
    enqueue call can return. Pop roots the record before clearing queue slots.
12. `process.nextTick` drains before Promise/microtask jobs; newly enqueued work
    follows the existing checkpoint ordering. Growth never changes ordering.
13. Unhandled-rejection candidates remain strongly reachable only until the
    checkpoint decision. Handled candidates are skipped, not removed by an
    unrooted pointer search.
14. Queue/drain safety policy may yield with work still queued; it never drops
    work to satisfy a fixed iteration/capacity constant.
15. All fallible helpers use the **D8.4.3** in-band error lane or an explicit
    checked status consumed by an in-band caller. Logging and returning success
    after a failed reaction/job append is forbidden.
16. Context reset and heap replacement clear all queue/async owner Items before
    destroying their heap. No old-heap Item remains in the stable capsule.
17. Diagnostics are context-local and cannot feed semantics under **D5.4.4**.
18. Every temporary dual reader has an exact phase owner and a zero end target.
19. At the third similar dynamic Item sequence, use the shared internal
    vector/deque helper; do not copy another grow/trace/clear implementation.
20. Every bug fix lands with a short root-cause/invariant comment at the fix
    point.
21. Promise and Lambda task remain two state machines. The shared kernel owns
    transport—queues, roots, wakeups, subscriptions—not `.then`, thenable
    assimilation, MIR continuation state, mailbox, join, or cancellation.
22. Shared async headers depend only on core runtime `Item`, rooting, and libuv
    boundaries. No `JsPromise`, JS closure, realm, species, or unhandled-
    rejection policy leaks into `lambda/runtime/`.
23. A completion subscriber owns its waiter/target as rooted Items, names one
    destination lane, is delivered at most once, and is cleared on delivery,
    unsubscribe, reset, or source finalization. A raw task/Promise pointer is
    never the durable subscription identity.
24. Under **D6.3.1**, Lambda-ready work is a FIFO macrotask lane and cannot run
    inside a JS microtask checkpoint. `nextTick` and JS microtasks remain
    separate logical lanes even when all lanes share one deque implementation.
25. Completion publication follows frontend state commitment. It may enqueue
    foreign work but never invokes the foreign continuation inline; value/error
    conversion remains at the JS/Lambda membrane.

---

## 4. T0 baseline and authoritative census

### 4.1 Current root cause, by owner

| Current owner | Current mechanism | Root problem |
|---|---|---|
| `js_runtime_state.hpp` | `JS_PROMISE_STATE_MAX 8192`; `JsPromiseRuntimeState::records` | Fixed-cap external lifetime table. |
| `JsPromise` | result plus four `[8]` Item arrays and `is_finally[8]` | Ninth pending reaction is not stored. |
| `js_promise_register_roots_once` | seven ranges per used record | Roots track allocation high-water, not reachability; replayed per heap epoch. |
| `js_alloc_promise` | monotonic table index | Records cannot be reclaimed/reused; 8,193rd allocation fails. |
| `js_promise_to_item` | separate Map plus `__promise_idx` | Two identities and an internal property protocol. |
| `js_promise_make_resolving_state` | `{idx, called}` Map | Executor closures retain an integer into external storage. |
| `JsAsyncContextStateRecord` | `promise_idx` | Suspended async state depends on table immortality. |
| `JsEventLoopQueueState` | four parallel arrays per lane, capacity 1,024 | Queue overflow logs and drops work. |
| `JsPromiseRuntimeState::unhandled_queue` | fixed 1,024 Item array | Unhandled candidate overflow logs and drops host reporting. |
| `vmap_gc_trace`/`vmap_gc_destroy` | unconditional HashMap-data interpretation | A Promise-specific VMap payload cannot be traced/finalized safely yet. |

Planning-tree counts on 2026-08-13 are informational, not acceptance
baselines: `js_promise_to_item` has about 20 references, `js_promises` about
23, `promise_idx` about 17, there are five fixed reaction-array declarations,
two `then_count < 8` branches, and three queue-overflow drop sites. T0 records
exact counts from a clean implementation base.

### 4.2 Baseline capture

Record the following before production changes:

```bash
git status --short
git rev-parse HEAD
./utils/count_loc.sh
rg -n "JS_PROMISE_STATE_MAX|JS_MAX_PROMISES|JS_PROMISE_UNHANDLED_QUEUE_MAX" lambda/js test utils
rg -n "js_promises|promises\.records|record_roots_(epoch|count)|js_promise_register_roots_once" lambda/js test utils
rg -n "js_promise_to_item|__promise_idx|promise_idx" lambda/js lambda/runtime test utils
rg -n "on_fulfilled\[8\]|on_rejected\[8\]|next_promise\[8\]|reaction_domain\[8\]|is_finally\[8\]|then_count < 8" lambda/js test
rg -n "JS_EVENT_MICROTASK_CAPACITY|MICROTASK_CAPACITY|queue overflow" lambda/js test
rg -n "js_promise_|JS_CLASS_PROMISE|Promise" lambda/js lambda/runtime test utils
rg -n "VMapVtable|vmap_gc_trace|vmap_gc_destroy|gc_vmap_(trace|destroy)_fn" lambda test/lib
```

Store full output, current test lists, compiler/platform details, debug and
release configurations, and benchmark commands under `temp/tune7_promise/`.
Never write Tune7 artifacts to `/tmp`.

Capture release behavior and cost before adding counters:

- complete `test_js_gtest` timing;
- focused Promise/async fixture timing;
- first-Promise allocation memory delta;
- 1, 8, 9, 1,024, 8,192, and 8,193 Promise/reaction/job cases;
- used Promise record high-water and registered-range count per batch;
- current Promise allocation/wrapper/job profile buckets; and
- Test262/Node passing and exclusion baselines for §10.

Old 7.1% profile data is historical evidence only. Acceptance compares a
fresh T0 release baseline with Tune7.

### 4.3 `utils/js_promise_census.py`

Add a checked-in census before carrier migration. It emits stable text/JSON
and classifies each Promise-related occurrence by:

- symbol/spelling and file/function;
- state owner (Promise, resolver closure, async context, job queue, module,
  host bridge, diagnostic);
- representation (table record, wrapper Map, index, VMap Item, raw derived
  pointer);
- lifetime/root owner;
- whether it crosses allocation, script re-entry, queue drain, or heap epoch;
- reaction/job/property/prototype/brand/diagnostic purpose;
- migration phase and deletion phase; and
- focused test owner.

The census separately inventories:

- all table constants, allocation/free/reset paths, range registrations, and
  table scans;
- all pointer subtraction/indexing and `promise_idx` fields/arguments;
- all wrapper/sentinel readers and writers;
- all fixed reaction arrays/caps and each reaction producer/consumer;
- all Promise Map-only type checks that must accept the VMap carrier;
- every `JsPromise*` local live across a `MAY_GC` or re-entry site;
- async function/generator/TLA/dynamic-import and concurrency bridge owners;
- async-hook/resource, domain, ALS, unhandled, and inspector paths;
- microtask/nextTick storage, root registration, enqueue, pop, flush,
  shutdown, reset, and overflow behavior; and
- VMap vtable definitions plus trace/destroy dispatch.

The script starts informational. Each phase ratchets completed groups. Any
residual allowlist names an exact function, reason, maximum count, and deletion
phase; wildcard exclusions fail.

---

## 5. Final architecture and internal contracts

### 5.1 Promise carrier

The final logical layout is:

```cpp
struct JsPromise : VMap {
    Item result;
    Item reactions;           // internal GC Array, flat typed records
    Item expando;             // null or metadata-qualified ordinary Map
    Item prototype_override;  // unset, explicit null, or explicit object
    Item reject_domain;
    uint64_t scalar_homes[5];
    int64_t unhandled_epoch;
    JsPromiseState state;
    uint16_t flags;
};
```

Names and packing may change after the allocation-class measurement, but the
ownership does not:

- allocate with `heap_calloc(sizeof(JsPromise), LMD_TYPE_VMAP)`;
- initialize the VMap prefix and one static `js_promise_vtable` before
  publication;
- keep `host_type` and `host_data` null; they are not Promise brand storage;
- make `data` point to the carrier only if the existing VMap callback ABI needs
  a payload argument; it is never a separately allocated Promise record;
- classify by `vtable == &js_promise_vtable`;
- trace the five Item slots and let the traced reaction Array/expando Map trace
  their own contents;
- finalize only Promise-owned native auxiliary storage, if any, exactly once;
  and
- compare object identity by the existing Item pointer identity.

Add static assertions for the VMap prefix, Item-slot contiguity, scalar-home
tail, alignment, and chosen GC size class. A size-class surprise is measured
and resolved deliberately; it is not worked around with an untyped side
allocation.

### 5.2 VMap lifecycle dispatch

Current mid-execution GC delegates every VMap to a runtime callback that casts
`VMap.data` as HashMap backing. That is insufficient for more than one native
VMap payload. T2 makes lifecycle selection part of `VMapVtable`, as
**D4.3.3** already requires.

Required end state:

1. `VMapVtable` has one optional allocation-free trace callback and its
   existing destroy callback.
2. The GC bridge receives the VMap object, reads its immutable vtable, and
   invokes that vtable's trace/destroy hooks.
3. The HashMap VMap supplies the existing key/value tracer and backing
   destroyer through its vtable.
4. Task/opaque handles supply null/no-op tracing where they own no Item edges.
5. Promise supplies its precise Item-edge tracer.
6. Host payload finalization remains distinct and occurs exactly once before
   backing destruction.
7. Mid-sweep finalization and context-wide finalization use the same dispatch
   helper and clear the same ownership token to prevent double destruction.

Do not add a Promise check in `gc_heap.c`; the collector knows only VMap and
calls the registered runtime bridge. Do not encode a tracer pointer in
`host_type`, `host_data`, or Promise data bytes. If exposing `gc_heap_t` in the
shared VMap header would violate layering, use a small marker callback
abstraction (`mark(context, Item)`) owned by the VMap runtime bridge.

### 5.3 Core-owned runtime async deque

The repository has fixed rooted `JsItemStack` and a pointer `ArrayList`, but no
growable FIFO whose Item storage remains visible to GC after relocation. Add
one small shared internal mechanism under `lambda/runtime/` only after T0
confirms that census:

```cpp
struct RuntimeAsyncDeque {
    Item storage;      // null or internal LMD_TYPE_ARRAY
    int64_t head;      // first live flat Item slot
    int64_t width;     // fixed Item count per record
};
```

It is a view over an ordinary GC Array, not a new allocator or value type. The
helper owns checked reserve/append, fixed-width record append/pop, consumed-slot
clearing, empty reset, optional prefix compaction, and context reset. Its rules:

- caller supplies record width; debug assertions keep `length - head`
  divisible by that width;
- growth roots the storage owner and every incoming Item, then reloads the
  Array/items pointer after allocation;
- append is atomic at record granularity; partial append rolls back and clears;
- pop roots output Items before clearing the owned slots;
- wide numeric Items use the Array's destination-owned scalar tail under
  **D5.2.2**;
- the helper never interprets a record's semantics or includes a JS header;
- lane instances belong to the context async kernel and are drained only by
  their frontend/checkpoint owner; sharing storage code does not merge lane
  priority; and
- no duplicate Promise-vector, microtask-ring, nextTick-ring, or unhandled-list
  growth code is added.

Use the helper for the reaction vector (width four), queued job records (width
four), and unhandled candidates (width one). If implementation evidence shows
that a vector-only wrapper is materially simpler for reactions, it must still
share reserve/store/clear code with the deque rather than become the third
near-identical dynamic Item container. R6a creates and switches the `nextTick`
and JS-microtask lane instances. R6b may add a Lambda-ready instance carrying
task-handle Items; it does not require a second queue implementation.

### 5.4 Reaction records and settlement

One reaction record contains exactly:

| Slot | Meaning |
|---|---|
| 0 | fulfillment handler or JS `undefined` |
| 1 | rejection handler or JS `undefined` |
| 2 | chained/native target Promise Item |
| 3 | captured domain/async context Item or null |

The current `is_finally[8]` field has no writers; `finally` already lowers to
normal wrapper handlers. Tune7 deletes it rather than copying dead state into
the new record.

`js_promise_add_reaction` is the only append owner. It receives rooted Items,
creates the reaction Array lazily, appends atomically, and returns an explicit
status consumed by `then`, adoption, async return adoption, and capability
forwarding. No caller writes vector slots directly.

Settlement order is frozen:

1. root the Promise Item and result;
2. verify `PENDING` and claim settlement;
3. publish destination-owned result and state;
4. capture and detach the reaction-vector Item from the Promise;
5. publish reject-domain/unhandled flags as applicable;
6. emit the Promise resolve async hook using the same Promise Item;
7. enqueue reactions in insertion order from the rooted detached vector;
8. clear consumed reaction slots once each queued job owns them; and
9. schedule the unhandled candidate only after rejection publication.

Handlers never run inline. A `.then()` added after settlement enqueues one job
directly. A `.then()` added while an earlier reaction job runs sees an already
settled Promise and joins the queue in normal FIFO order.

### 5.5 Item-first Promise API

P1 converts ownership before representation. Final internal semantics use
Promise Items:

```cpp
bool       js_promise_is(Item value);
JsPromise* js_promise_from_item(Item rooted_value); // derived, non-owning
Item       js_promise_new_pending(void);
Item       js_promise_resolve_value(Item promise, Item value);
Item       js_promise_settle(Item promise, JsPromiseState state, Item result);
Item       js_promise_add_reaction(Item promise, Item on_fulfilled,
                                   Item on_rejected, Item next, Item domain);
```

Exact names/signatures may follow current public ABI constraints, but these
rules do not change:

- allocation returns the owning Item, not `JsPromise*`;
- `js_promise_from_item` performs only type/vtable validation and a cast;
- fallible operations return the D8.4.3 lane or checked status;
- a raw derived pointer is reacquired after any allocating/re-entering call;
- `js_promise_to_item` has no final role and is deleted, not retained as an
  identity helper; and
- public functions in `js_runtime.h` keep their observable contracts while
  internal pointer variants disappear.

Executor resolving state becomes `{ promise, called }`, retaining the Promise
Item directly. `JsAsyncContextStateRecord::promise_idx` becomes an Item field
included in that context's exact roots. TLA/module fields already store Items
and need brand/accessor migration only.

### 5.6 Promise object/property behavior

`JS_CLASS_PROMISE` metadata gains native/exotic flags and
`js_promise_property_ops`. `js_object_meta` checks Promise vtable identity
before the generic host-VMap branch and returns the immutable Promise metadata.

The operations table follows Tune5/Tune6 contracts:

- get/has/descriptor/keys/delete consult the lazy expando Map only;
- a missing read does not allocate and falls through to the intrinsic/custom
  prototype walk;
- set/define create the expando only after extensibility and descriptor policy
  allow the mutation;
- ordinary Map storage owns descriptor flags, Symbol identity, insertion order,
  and scalar homes—Promise code does not duplicate shape semantics;
- get/set prototype use the explicit override field and the rooted realm
  `Promise.prototype` fallback;
- explicit null is distinguishable from "no override" by a flag, not a
  sentinel property;
- is/prevent-extensible use a Promise flag and reject later new expandos while
  allowing valid updates to existing writable properties; and
- internal state/result/reactions are invisible to all property/reflection
  operations.

The new object must satisfy `Promise.resolve(p) === p`, `p instanceof Promise`,
`Object.getPrototypeOf(p) === Promise.prototype`, no own `constructor` on a
fresh Promise, and normal inherited `constructor` behavior. Tune7 does not
preserve the wrapper's non-spec own `constructor` property.

### 5.7 Job queues and unhandled rejections

There remains one microtask mechanism. `js_enqueue_promise_job` validates and
forwards into the same microtask lane used by `queueMicrotask`; it does not
create a Promise-only queue.

Each queued job record contains:

1. callback;
2. async-hooks resource;
3. AsyncLocalStorage context; and
4. domain stack/context.

The runtime owns two logical deque instances using the one §5.3
implementation:

- `nextTick`, drained first; and
- Promise jobs/`queueMicrotask`, drained second.

The outer loop repeats until both are empty, preserving the current rule that
nextTicks queued by a microtask run before the next microtask batch. Any host
anti-hang work budget leaves remaining records queued for a later turn; it
does not discard them or treat queue capacity as a fairness policy.

JR7's unhandled-rejection open question is resolved as follows:

- on rejection without a handler, append the Promise Item once to a strong
  candidate deque and set `unhandled_check_scheduled`;
- drain a snapshot only after the complete nextTick/microtask checkpoint;
- skip a candidate if it became handled, changed listener epoch, or was already
  reported;
- emit `unhandledRejection` at most once and preserve domain/strict policy;
- a later handler on a reported, still-reachable Promise emits
  `rejectionHandled` at most once; and
- clear the candidate slot immediately after the check, allowing collection.

A weak list is rejected: the host event itself needs a stable Promise Item at
the checkpoint, weak semantics are not otherwise available, and adding them
would be a second lifetime mechanism. A permanent strong list is rejected
because it recreates the table retention bug.

### 5.8 Native completion subscriptions — JS side first

JR7.1 adds one core-owned one-shot completion transport. It is a subscriber
port, not a replacement for Promise or task state:

```text
frontend commits terminal state
            |
            v
 publish(outcome) ---> rooted subscription ---> enqueue(target lane)
```

The concrete C+ layout is chosen after the T0 lifetime census, but the ABI has
these fixed properties:

- a source owns a growable collection of subscription records;
- each record owns a target/waiter `Item`, a small core-defined delivery kind,
  a destination lane, and any additional Item payload required by that kind;
- the source frontend publishes `{fulfilled|rejected|error|cancelled, Item}`
  exactly once after its own terminal state/result is visible;
- publish detaches the collection, transfers each record to its destination
  lane in registration order, and clears the detached storage;
- unsubscribe/target cancellation marks or removes only that subscription;
  cancellation does not flow into a Promise source;
- a late subscriber is handled from the frontend's rooted terminal result and
  is still enqueued rather than called inline; and
- unknown delivery kinds fail at the checked native boundary; they never fall
  through to JS property lookup or a generic callback cast.

Tune7/R6a implements the shared port and the Promise publisher. Promise's
ordinary JS reactions remain the four-Item records of §5.4, because species,
handler selection, and chained-Promise resolution are ECMAScript semantics,
not generic completion transport. A native subscription coexists with those
reactions but does not allocate a JS function or call `.then`.

R6b is the explicit Lambda-side follow-up:

1. embed/use the same completion publisher in `LambdaTask` terminal paths;
2. migrate Lambda FIFO runnable/resume work to the Lambda-ready lane;
3. replace `LambdaPromiseObserver` in `concurrency_js.cpp` with a task-source
   subscription whose target is the Promise Item;
4. replace the two `js_new_native_closure` + `js_promise_then` wake handlers
   with a Promise-source subscription whose target is the task-handle Item;
5. translate JS rejection, Lambda `T^E`, and cancellation only in the membrane
   delivery adapters; and
6. delete the old observer/root registration and closure environments after
   both directions pass the same ordering/GC tests.

The R6a compatibility bridge may observe the new Promise carrier through
`js_promise_is` and Item-first settlement APIs, but Tune7 does not partially
rewrite `LambdaTask`. This sequencing keeps the implementation bisectable and
protects **D6.3.1**: Promise reactions drain in the JS microtask lane; a task
resumed from Promise settlement enters the FIFO Lambda macrotask lane only
after the checkpoint.

### 5.9 Diagnostics and pending count

Replace the table scan in `js_promise_pending_count` with context-local scalar
counters:

- allocated, settled, finalized, live, live-pending, and peak-live Promises;
- reaction appends/growth/peak per Promise;
- queued job appends/growth/peak depth;
- unhandled candidates/report/handled-late counts; and
- lazy expando creation count.

Only the minimal counters required by product behavior remain always-on.
Detailed counters are compile-time or environment-gated, are reset with the
context, and never affect semantics under **D5.4.4**. Finalization decrements
live/pending diagnostics without allocating or invoking script. T0 must verify
whether `js_promise_pending_count` is product policy or diagnostic before its
counter becomes an acceptance dependency.

---

## 6. Phase dependency graph

```text
T0  clean handoff + adopted D7.4.1v2 + census + behavior locks
 |
 v
T1  Item-first ownership while the old carrier remains
 |
 v
T2  per-vtable VMap trace/finalize support
 |
 v
T3  GC Promise carrier + growable reactions + property ops
 |   + Promise-side native completion publisher
 |   (switch producers, then delete table/wrapper/index in the same phase)
 v
T4  core async deque/lane kernel + JS job/nextTick lanes
 |   + strong unhandled candidate queue
 |
 v
T5  secondary consumers, reset/teardown, structural deletion sweep
 |
 v
T6  full conformance, release performance, docs, and JR8 handoff
```

T1 and T2 may proceed independently after T0, but T3 requires both. T4 may
prototype the core deque/lane kernel after T0; it does not land its production
JS switch until T3 establishes the final Promise Item owner. R6b follows T6
and is intentionally outside Tune7: it migrates the Lambda-ready lane and task
completion publisher only after the JS contracts are green.

---

## 7. Detailed implementation phases

### T0 — Formal adoption, clean baseline, census, and behavior locks

#### Objectives

- Freeze the exact Tune6 handoff.
- Verify the adopted formal carrier authority before code depends on it.
- Make current caps, ownership, ordering, and known failures reproducible.

#### Work

1. Capture §4.2 baseline evidence under `temp/tune7_promise/`.
2. Add `utils/js_promise_census.py` and an informational report.
3. Record adopted D7.4.1v2 as the carrier authority and verify JR7's resolved
   unhandled-rejection choice remains aligned with §5.7.
4. Add passing behavior locks for current correct resolution, thenable,
   species, combinator, ordering, async hook/domain/ALS, reflection, reset, and
   concurrency behavior.
5. Add correct-output defect fixtures for ninth reaction, 8,193rd allocation,
   and queue growth beyond 1,024. Record them as expected-red until their
   owning phases; do not weaken their expected output to match silent drops.
6. Capture fresh release and release-profile measurements.
7. Record exact Test262 and Node baseline lists; Tune7 cannot grow exclusions
   without a root cause and explicit review.

#### Exit gate

- Formal and vibe documents agree on the carrier.
- Census covers every T0 category with no wildcard.
- Existing green gates remain green; expected-red cap fixtures fail for the
  recorded root cause only.
- No production representation change has landed.

### T1 — Convert table-index ownership to Promise Item ownership

#### Objectives

- Remove index dependence from all long-lived consumers before changing the
  carrier.
- Make raw-pointer lifetimes explicit while the old table still provides a
  stable compatibility base.

#### Work

1. Change resolver shared state from `{idx, called}` to `{promise, called}`.
2. Make resolve/reject callbacks validate the retained Item and call Item-first
   settle/resolve helpers.
3. Change `JsAsyncContextStateRecord::promise_idx` to `Item promise`; add the
   field to exact context roots before first publication.
4. Convert async drive/start/get-promise paths from table indexing to the Item.
5. Convert settle, adopt, thenable jobs, reaction jobs, `await`, async return,
   combinators, TLA, and diagnostics so an owning Item is rooted whenever a
   derived `JsPromise*` crosses allocation or re-entry.
6. Keep `js_promise_to_item` only as the old-carrier boundary during T1; no new
   caller may accept or store an index.
7. Add forced-GC tests around resolver creation, executor invocation, async
   suspension/resume, returned-Promise adoption, bounded libuv drain, and
   module awaited-target capture.
8. Ratchet every `promise_idx` use except the old wrapper's private local index
   to zero.

#### Exit gate

- No struct field, callback argument, bound environment, module record, or host
  bridge stores a Promise table index.
- Every remaining integer index occurrence is inside the T3-owned old-wrapper
  bridge and exact census allowlist.
- Forced-GC and async/TLA/concurrency gates pass on the old representation.

### T2 — Make VMap tracing and destruction backend-specific

#### Objectives

- Fulfill D4.3.3 for heterogeneous VMap payloads.
- Establish and test the lifecycle hook needed by Promise before Promise
  allocation changes.

#### Work

1. Extend the internal VMap vtable with optional trace support without
   exposing collector internals across an inappropriate header boundary.
2. Change `gc_vmap_trace_fn`/bridge plumbing to dispatch from the VMap object,
   not blindly from `data` as `HashMapData`.
3. Route mid-sweep and context-finalize destruction through one vtable-aware,
   idempotent helper.
4. Install the existing HashMap trace/destroy functions on its vtable.
5. Audit task handles and every other VMap vtable; declare exact null/no-op or
   real Item-edge ownership.
6. Update `test/lib/test_gc_heap_gtest.cpp` for object-aware callback dispatch,
   null-data safety, backend distinction, one-time destroy, and child liveness.
7. Add a test VMap backend with non-HashMap Item edges to prove the collector
   does not assume one payload type.
8. Preserve host payload destruction ordering and Jube/module behavior.

#### Exit gate

- Generic HashMap VMap tests and all existing VMap/Jube/concurrency tests pass.
- Two VMap backends with different payload layouts trace/finalize correctly.
- `gc_heap.c` contains no Promise or JS class knowledge.
- No VMap payload is cast to HashMap data before vtable selection.

### T3 — Publish the final Promise carrier and delete the table

#### Objectives

- Move native Promise lifetime onto the GC heap.
- Make reaction storage growable.
- Remove the wrapper/index/static-root architecture completely.

#### Work

1. Add the coherent Promise carrier header/implementation and static vtable.
   Keep algorithms in their current TU initially; JR10 owns broad movement.
2. Add Promise trace/finalize callbacks and allocation/layout assertions.
3. Add the core runtime async Item deque after its T0 no-duplicate audit.
4. Implement one reaction append API and migrate adoption, `.then`, async
   return, await resume/reject, species capability forwarding, and all other
   reaction producers.
5. Implement settlement by detaching and draining the rooted reaction vector.
6. Add Promise metadata resolution and `JsPropertyOps`; implement lazy expando,
   intrinsic/custom/null prototype, descriptor, Symbol, delete, own-keys, and
   extensibility behavior through the existing property kernel.
7. Change `js_alloc_promise`/construction paths to allocate and immediately
   return the VMap Item. Preserve async-hooks resource identity.
8. Switch `js_promise_is`, state inspection, util/assert/Node brand consumers,
   Promise.resolve fast path, TLA, and Lambda concurrency to vtable identity.
9. Add the core-owned one-shot completion-subscription primitive and embed its
   subscriber owner in the Promise carrier. Publish only after settlement state
   and result are committed; root, detach, enqueue, and clear records per §5.8.
10. Add native Promise subscription tests with a core test delivery kind,
    including pending and late subscription, fulfillment/rejection, multiple
    subscribers, unsubscribe, forced GC, reset, and no-inline-delivery checks.
11. Run dual-reader compatibility only inside the migration batch if needed:
   first teach the reader both forms, switch every producer, prove no old-form
   producer, then remove the old form before T3 exits.
12. Delete `JS_PROMISE_STATE_MAX`, `JS_MAX_PROMISES`, `records`, `count`,
    record root epochs/counts, table allocation/free/reset, wrapper fields,
    `js_promise_to_item`, `__promise_idx`, pointer subtraction, table indexing,
    fixed reaction arrays, `then_count`, and capacity logging.
13. Remove the JR7 exception from `utils/js_object_census.py` rather than
    leaving a zero-count allowlist.
14. Turn the ninth-reaction and 8,193rd-Promise fixtures green.

#### Exit gate

- One Promise representation and one identity remain.
- Ninth and substantially larger reaction fan-out preserve every callback in
  order; adoption and chained results are correct.
- More than 8,192 sequential/live allocations do not hit a semantic cap.
- Fresh Promise reflection exposes no own internal/constructor property;
  expandos/prototypes/descriptors work.
- Promise settlement publishes native subscriptions exactly once without JS
  closure allocation or inline foreign continuation execution.
- Forced GC preserves all live edges and reclaims unreachable Promises.
- All table, wrapper, sentinel, fixed-reaction, and per-record-root ratchets are
  zero.

### T4 — Land the shared async kernel and migrate JS lanes first

#### Objectives

- Remove fixed-capacity loss from Promise jobs, `queueMicrotask`, nextTick, and
  unhandled-rejection reporting.
- Establish the core-owned deque/lane contract that R6b can reuse.
- Preserve event-loop ordering and distinct semantic lanes over one queue
  implementation.

#### Work

1. Add `RuntimeAsyncDeque` and lane/checkpoint vocabulary under
   `lambda/runtime/`; depend only on core Item/root/runtime facilities.
2. Unit-test reserve/append/pop/compact/reset and multiple independent lanes
   without importing a JS header.
3. Represent each JS queue entry as the four-Item record in §5.7.
4. Replace the microtask parallel arrays with one JS-microtask lane instance.
5. Replace the nextTick parallel arrays with a second instance of the same
   core implementation; preserve nextTick priority rather than merging
   semantic lanes.
6. Reserve the Lambda-ready lane identity and checkpoint barrier in the core
   contract, but do not switch `LambdaScheduler::run_head/run_tail` in R6a.
7. Root only the stable context-owned storage Items through the existing
   `JsRootRange` construction/reset path.
8. Reload Array/items pointers after every enqueue/drain allocation.
9. Change enqueue APIs to return checked status and migrate callers so growth
   failure is never logged-and-dropped.
10. Replace capacity-derived flush safety with a work/host-watchdog policy that
   leaves undrained jobs queued.
11. Replace `unhandled_queue[1024]` with the strong candidate deque and implement
   the checkpoint algorithm in §5.7.
12. Update pending-count, drain-progress, bounded-await, shutdown, batch-reset,
   and event-loop reset logic for deque depth rather than ring counters.
13. Turn the more-than-1,024 job and unhandled-candidate fixtures green.

#### Exit gate

- Promise/`queueMicrotask` FIFO, nested scheduling, nextTick priority, timers,
  domains, ALS, and async-hook resource order are byte-identical to the locked
  behavior.
- Growth cases lose no callback and preserve order.
- Core async code has no JS semantic dependency; JS owns only record creation
  and lane drain behavior.
- Lambda scheduling code is behaviorally unchanged, and an interop ordering
  lock proves its resumes still occur after the JS checkpoint.
- Queue shutdown/reset leaves no stale heap Item.
- Fixed microtask/unhandled capacity and overflow-drop branches are zero.

### T5 — Secondary consumers, reset/teardown, and structural deletion sweep

#### Objectives

- Close every non-core Promise assumption and lifetime seam.
- Prove predecessor deletion structurally.

#### Work

1. Audit/migrate `js_util`, `js_assert`, Node APIs, streams, fetch/fs/net/tls,
   clipboard, timers, Atomics, dynamic import/TLA, async generators, DOM/Radiant,
   and Lambda concurrency bridges.
2. Replace Promise-specific `get_type_id(...) == LMD_TYPE_MAP` gates with the
   semantic brand helper where Promise VMap is valid; do not broaden unrelated
   Map-only algorithms blindly.
3. Verify inspector/state-name output without property lookup or allocation on
   the brand path.
4. Remove Promise-table cleanup from `js_deep_batch_reset` and context destroy.
   Clear queue/async owner roots and counters before heap replacement.
5. Exercise finalization during normal GC, batch heap replacement, event-loop
   shutdown, and context teardown; prove exactly-once behavior.
6. Run the census in check mode and delete all compatibility allowlists.
7. Record production LOC and complete the deletion ledger. Simplify rather
   than moving old code to satisfy the net-negative gate.
8. Freeze the R6b handoff: document the Promise native-subscribe ABI, the
   Lambda-ready lane barrier, current bridge call sites, and exact observer/
   closure symbols that the Lambda follow-up must delete. Do not partially
   migrate task frame/scheduler ownership in this sweep.

#### Exit gate

- Structural targets in §11 are all met.
- No Promise Map-only assumption remains in a Promise semantic consumer.
- The current Lambda bridge passes against the final Promise Item API, and its
  R6b deletion allowlist is exact and introduces no Promise table/index state.
- Multi-context, multi-epoch, batch-reset, and teardown tests pass under forced
  GC and freed-memory poisoning.
- Production C/C++ delta is net-negative.

### T6 — Full validation, release performance, docs, and handoff

#### Objectives

- Establish final behavior, conformance, lifetime, and performance evidence.
- Make the implemented design discoverable and hand JR8 stable seams.

#### Work

1. Run the complete §12 command sequence from a clean build.
2. Run interleaved release A/B performance capture using §13.
3. Use `release_profile` only for attribution; acceptance timings use
   `make release` per project rule 10.
4. Update `doc/dev/js/JS_09_Async_Modules.md` and its Promise/job diagram from
   fixed pool/rings to GC carrier/growable deques.
5. Update `doc/dev/js/JS_16_Testing.md`, JS overview/performance notes, and any
   stale fixed-cap line references.
6. Update the redesign roadmap status and remove its JR7 open question.
7. Record exact test counts, exclusions, profile buckets, LOC, and residual
   risks in an implementation-evidence appendix. Do not mark this plan done
   until those facts exist.

#### Exit gate

- All required suites pass with no unexplained baseline loss.
- Release performance meets §13.
- Docs describe the implemented code, not this proposal.
- JR8 can consume Promise/property/call feedback without any Promise
  representation or lifetime bridge.

---

## 8. File and ownership map

| File / area | Tune7 ownership |
|---|---|
| `doc/Lambda_Formal_Design.md` | Adopted D7.4.1v2 authority for the Promise carrier. |
| `vibe/jube/JS_Runtime_Redesign.md` | Resolve JR7 representation/unhandled decision and phase status. |
| `lambda/lambda.hpp` | Internal VMap vtable trace contract and layout assertions. |
| `lambda/runtime/gc/gc_heap.h/.c` | Object-aware generic VMap trace/destroy callback boundary; no Promise knowledge. |
| `lambda/runtime/vmap.cpp` | HashMap VMap vtable trace/destroy implementation and shared dispatch bridge. |
| `lambda/runtime/lambda-mem.cpp` | One VMap host/backing finalization path for sweep and teardown. |
| `lambda/js/js_promise.h/.cpp` (expected) | Promise carrier, vtable, trace/finalize, reaction vector, brand/accessors, and Promise-side native completion publisher. Do not move the entire algorithm monolith before representation is green. |
| `lambda/js/js_runtime.cpp` | Promise Resolution Procedure, settlement/capabilities/combinators/await migration, old table deletion. |
| `lambda/js/js_runtime.h` | Stable public Promise API; remove index-oriented internals. |
| `lambda/js/js_object_meta.h/.cpp` | Promise vtable brand -> JS_CLASS_PROMISE metadata and Promise ops table selection. |
| `lambda/js/js_props.*`, `js_globals.cpp` | Promise expando/prototype/extensibility operations through the existing eight-operation ABI. |
| `lambda/runtime/async.h/.cpp` (expected, name reviewable) | JS-neutral `RuntimeAsyncDeque`, lane/checkpoint contract, rooted native completion subscriptions, and diagnostics. No Promise/task semantic policy. |
| `lambda/js/js_runtime_state.hpp/.cpp` | Remove Promise records/fixed arrays; root queue owner Items and async-context Promise Items; counters/reset. |
| `lambda/js/js_event_loop.h/.cpp` | JS clients of the core nextTick/microtask lane instances; drain/reset/shutdown/progress behavior. |
| `lambda/js/js_job_queue.h/.cpp` | PromiseJob adapter and checked enqueue result; no second queue. |
| `lambda/js/js_util.cpp`, `js_assert.cpp` | Promise VMap brand/inspection/assertion migration. |
| `lambda/runtime/concurrency_js.cpp` | R6a compatibility only: replace Map/index assumptions and pass final Item-first Promise behavior. R6b owns observer/closure deletion. |
| `lambda/runtime/concurrency.cpp` / `concurrency.h` | No R6a scheduler/task-state migration; record the exact R6b Lambda-ready/completion-publisher handoff. |
| Node/Web/DOM/Jube consumers | Replace Map/index assumptions while preserving host behavior. |
| `build_lambda_config.json` | Add new coherent sources; regenerate build files with `make`. Never edit `.lua` files. |
| `utils/js_promise_census.py` | Authoritative Tune7 structural manifest and ratchets. |
| `test/lib/test_gc_heap_gtest.cpp` | Multi-backend VMap trace/finalize lifecycle tests. |
| `test/js/`, `test/node/`, Test262 baseline | Promise, queue, async, reflection, ordering, and lifetime behavior. |
| `doc/dev/js/JS_09_Async_Modules.md` | Final implemented Promise/job design and diagrams. |

If the carrier can live coherently in an existing Promise-specific module
without duplication, use it. Do not perform JR10's unrelated 40k-TU split in
Tune7. Any new `.cpp` entry is added through `build_lambda_config.json`, then
generated by `make`; generated Lua is never edited.

---

## 9. Migration ledger

| Current mechanism | Transitional owner | Final owner | Delete by |
|---|---|---|---|
| `JsPromiseRuntimeState::records` slab | T3 dual reader only if necessary | GC heap | T3 |
| `JS_PROMISE_STATE_MAX` / `JS_MAX_PROMISES` | none | heap capacity | T3 |
| `js_promise_count` table high-water | T3 diagnostics bridge | live/pending scalar diagnostics | T3 |
| seven per-record root ranges | none after carrier switch | Promise VMap trace | T3 |
| Map wrapper | T3 dual reader only | Promise VMap Item | T3 |
| `__promise_idx` | T3 exact old-reader bridge | none | T3 |
| `js_promise_to_item` | T1/T3 old carrier boundary | direct Item identity | T3 |
| resolver `{idx, called}` | none after T1 | `{promise, called}` | T1 |
| async-context `promise_idx` | none after T1 | rooted Promise Item | T1 |
| four fixed reaction arrays | none after T3 switch | flat growable reaction records | T3 |
| `is_finally[8]` | none | existing finally wrapper handlers | T3 |
| `then_count` and `< 8` guards | none | vector record count | T3 |
| wrapper own `constructor` | none | inherited Promise.prototype property | T3 |
| microtask parallel rings | T4 migration batch | core async deque, JS-microtask lane | T4 |
| nextTick parallel rings | T4 migration batch | core async deque, nextTick lane | T4 |
| capacity-derived flush limit | T4 migration batch | non-dropping host work budget | T4 |
| fixed unhandled candidate array | T4 migration batch | strong checkpoint deque | T4 |
| HashMap-assuming VMap GC bridge | none after T2 | per-vtable lifecycle dispatch | T2 |

No row may finish with both predecessor and final owner active.

### 9.1 Explicit R6b handoff (not a Tune7 exit dependency)

| Existing Lambda-side mechanism retained in R6a | R6b final owner | R6b deletion proof |
|---|---|---|
| `LambdaScheduler::run_head/run_tail` FIFO | core async deque, Lambda-ready lane | no second runnable-queue implementation; D6.3.1 ordering differential |
| `LambdaPromiseObserver` + manually registered Promise root | task completion subscription targeting a Promise Item | observer type/root callbacks absent; task -> Promise GC/order tests |
| two native JS closures/environments + `js_promise_then` used to wake a task | Promise completion subscription targeting a task-handle Item | closure callbacks/envs absent; Promise -> task fulfill/reject/cancel tests |
| bridge-local error conversion | membrane delivery adapters | one conversion owner per direction; `T^E` and JS rejection matrix |

Tune7 must not disguise these rows as completed unification. Its job is to
land and prove the core contract plus its JS producer/queue clients so R6b is
a consumer migration, not a redesign.

---

## 10. Test strategy

Every new `test/js/*.js` fixture gets its matching expected `.txt`. If a new
Lambda `*.ls` test is needed for the Promise membrane, its expected `.txt` is
mandatory. Correct-output cap fixtures may be recorded red only until their
named phase; they are not added to a green aggregate target prematurely.

### 10.1 Core Promise and resolution matrix

- executor callable validation and throw -> rejection;
- resolve/reject first-call-wins shared state;
- self-resolution cycle rejection;
- native Promise adoption, thenable assimilation as a job, throwing `then`
  getter/call, and resolve-then-throw;
- `Promise.resolve(p) === p` and constructor identity;
- handlers before/after settlement and handlers added by another handler;
- missing fulfillment/rejection pass-through;
- handler return/throw/native Promise/foreign thenable;
- `catch` and all `finally` pass/reject/cleanup-failure cases;
- `Symbol.species`, custom capability constructors, subclasses, bound
  constructors, and replaced prototype;
- all/race/any/allSettled empty/non-empty/iterator/double-call/error cases; and
- `Promise.withResolvers`, async functions/generators, and Lambda Promise
  membrane.

### 10.2 Growth and ordering matrix

| Case | Required result |
|---|---|
| 1, 8, 9, 32, and large reaction fan-out on one pending Promise | Every reaction runs once in registration order. |
| More than 8 adopted/native reactions | Every target settles correctly. |
| More than 8,192 sequential Promise allocations | No representation-cap failure. |
| More than 8,192 simultaneously live Promises | Correct until genuine allocation pressure; no index alias. |
| More than 1,024 Promise jobs | No drop; FIFO preserved. |
| More than 1,024 `queueMicrotask` calls | No drop; shared FIFO preserved. |
| More than 1,024 nextTicks | No drop; nextTick priority preserved. |
| Nested nextTick from microtask and microtask from nextTick | Existing checkpoint order preserved. |
| Timer/uv callback enqueues Promise jobs | Jobs drain at the same phase checkpoints. |
| Work-budget exhaustion | Remaining work stays queued and executes later. |
| Promise native subscriber registered while pending | Delivery is queued once in registration order; no JS closure and no inline callback. |
| Promise native subscriber registered after settlement | Rooted terminal outcome is queued once on the named lane, never invoked inline. |
| Lambda task waits on a Promise through the R6a compatibility bridge | All JS nextTick/microtasks at the checkpoint finish before the FIFO Lambda resume. |
| Lambda task completion settles a JS Promise through the R6a bridge | Settlement is visible once; reactions run in the next JS microtask checkpoint. |
| Waiting Lambda task is cancelled | Waiter stops/resumes per task policy; source Promise is not cancelled or mutated. |

### 10.3 Object, prototype, and reflection matrix

- fresh Promise has zero engine-internal own keys and no own `constructor`;
- `Object.keys`, `getOwnPropertyNames`, `getOwnPropertySymbols`,
  `Reflect.ownKeys`, descriptors, JSON/stringification, spread, and inspection;
- string and Symbol expandos, insertion order, update, delete, redefine,
  writable/enumerable/configurable attributes;
- `Object.preventExtensions`/seal/freeze behavior as supported by the shared
  property core;
- default `Promise.prototype`, custom object prototype, explicit null,
  cycle/non-extensible failures, and restoration policy;
- inherited accessor Receiver is the Promise;
- private-name/WeakMap/object-like use where supported;
- vtable/brand spoofing by `__promise_idx`, `constructor`, `toStringTag`, name,
  or ordinary VMap is impossible; and
- two realms/contexts resolve their own Promise prototype without static Item
  metadata.

### 10.4 GC and lifetime matrix

Force GC with `LAMBDA_GC_FORCE_EVERY=1` and
`LAMBDA_GC_POISON_FREED=1`:

- immediately after Promise allocation and before publication;
- while creating resolve/reject closures;
- while appending/growing reactions;
- after result publication for object, string, float, int64, and error values;
- with live fulfillment/rejection handlers, chained Promise, domain, custom
  prototype, and expando values;
- after reaction detachment but before every job is enqueued;
- after queue pop clears persistent storage but before callback invocation;
- across async suspension, uv/timer delay, async generator, TLA/dynamic import,
  and Lambda task wait;
- after a settled or pending Promise becomes unreachable;
- during unhandled candidate/report/late-handler paths;
- across batch heap replacement, deep reset, context destroy, and repeated
  context creation; and
- with VMap finalizer called by normal sweep and final heap teardown.

Assert live edges survive, dead Promises become collectible, auxiliary storage
is destroyed exactly once, counters do not underflow, and no raw pointer from a
retired heap is observed.

### 10.5 Unhandled rejection matrix

- handler attached before checkpoint suppresses reporting;
- rejection without handler reports once with exact Promise identity/reason;
- late handler after report emits `rejectionHandled` once;
- repeated `.catch()` does not repeat the event;
- strict mode and `uncaughtException` routing preserve current policy;
- domain error routing and listener removal epoch preserve current behavior;
- candidate queue growth cannot drop a report;
- handled candidate releases its strong queue root after the checkpoint; and
- context shutdown/reset does not report stale prior-context rejection.

### 10.6 VMap lifecycle matrix

- ordinary HashMap VMap key/value liveness;
- null data/vtable callbacks;
- task/opaque handle no-op trace;
- Promise embedded Item fields;
- different vtables with different data layouts in the same collection;
- host payload plus backing destroy order;
- mid-sweep and context-finalize exactly once; and
- callback allocation/re-entry prohibition in debug validation.

### 10.7 Test262 and integration groups

Run at least the supported baseline slices for:

- `built-ins/Promise`, Promise prototype/statics/combinators/species;
- `language/expressions/async-*`, async functions/methods/generators;
- `await`, thenables, async iteration, and module/dynamic-import cases;
- host hooks used by async Test262 `$DONE`;
- Object/Reflect/prototype/descriptor operations on Promise objects;
- Node promise, microtask, nextTick, async-hooks, domains, ALS, util.types,
  streams, timers, fs/net/fetch, and unhandled-rejection tests;
- Lambda/Jube concurrency Promise conversion and wait; and
- Radiant/DOM async fetch/timer/layout cases.

The T0 passing/exclusion sets are frozen. A newly passing cap test is admitted;
a newly failing unrelated test blocks the phase.

---

## 11. Structural ratchets and LOC

At T6, `utils/js_promise_census.py --check` enforces:

| Ratchet | Final target |
|---|---:|
| Native Promise representation kinds | 1 |
| Promise vtable definitions | 1 |
| `JS_PROMISE_STATE_MAX` / `JS_MAX_PROMISES` | 0 |
| Promise `records` slab fields/alloc/free/reset | 0 |
| Promise record high-water index semantics | 0 |
| `js_promises[...]` / pointer subtraction from slab | 0 |
| `promise_idx` fields/arguments/locals with semantic ownership | 0 |
| `js_promise_to_item` | 0 |
| `__promise_idx` production/test/census allowlist references | 0 |
| Promise wrapper/wrapper-created fields | 0 |
| `js_promise_register_roots_once` | 0 |
| Per-Promise root-range registrations | 0 |
| Fixed Promise reaction arrays | 0 |
| `then_count < 8` capacity branches | 0 |
| Promise reaction append owners | 1 |
| Promise semantic brand helpers | 1 boundary (`js_promise_is`/shared resolver) |
| Promise brand by Map-only test/name/property/host_type/map_kind | 0 |
| Promise raw pointers crossing MAY_GC without owner root | 0 by audit/checker |
| Core runtime async Item vector/deque growth implementations | 1 |
| JS-specific growable queue implementations | 0 |
| Core async headers depending on JS/Promise semantic headers | 0 |
| Promise native completion publish owners | 1 |
| Promise native subscription paths allocating JS closures/calling `.then` | 0 |
| Lambda-ready queue migrations in R6a | 0 (reserved contract only; R6b owner) |
| Fixed Promise/microtask capacity drop branches | 0 |
| Fixed unhandled-rejection array/cap | 0 |
| Promise-only queue mechanisms beside microtask queue | 0 |
| Generic VMap GC paths assuming HashMap payload before vtable dispatch | 0 |
| Conservative native-stack Promise rooting | 0 |
| C2MIR or vendor production diffs | 0 |

Record T0/T6:

```bash
./utils/count_loc.sh
wc -l lambda/js/js_runtime.cpp lambda/js/js_event_loop.cpp \
      lambda/js/js_job_queue.cpp lambda/js/js_runtime_state.cpp \
      lambda/js/js_runtime_state.hpp lambda/runtime/async.cpp \
      lambda/runtime/vmap.cpp \
      lambda/runtime/lambda-mem.cpp lambda/runtime/gc/gc_heap.c
```

Count all new Promise/deque files in the final production total. Moving the
same code between files, generated-file churn, tests, docs, or unrelated
deletion does not satisfy the net-negative gate. The implementation should
delete more wrapper/table/root/fixed-ring code than the carrier, tracer, and
deque add. If it does not, simplify the ownership surface before landing.

---

## 12. Validation commands and batch gates

Confirm exact targets at T0. Expected gate:

```bash
make build
make build-test
./test/test_js_gtest.exe
make test-lambda-baseline
make test262-baseline
make node-regression-gate
make test-mir-gc-stress
make test-gc-rooting-core
make test-jube
make test-radiant-baseline
make lint
```

Run focused Promise/VMap tests after every logical batch. Run broad JS and
forced-GC gates after T1, T2, and each T3 carrier batch. Run Node/Jube/Radiant
gates after consumer migrations and the complete set at T6.

Structural/release closure:

```bash
make release
./utils/js_promise_census.py --check
./utils/js_object_census.py --check
./utils/count_loc.sh
```

Do not use a debug binary for performance conclusions. Do not edit generated
Lua build files; edit `build_lambda_config.json` and run `make`. All transient
outputs stay under `./temp/`.

---

## 13. Release performance acceptance

### 13.1 Measurements

Build clean baseline and candidate release binaries and run them in
row-interleaved A/B order. Record at least five samples, medians, dispersion,
hardware/OS, compiler, commit, and exact commands.

Measure:

- first Promise construction and large batches of construction/settlement;
- pending reaction append at 1/8/9/32/large fan-out;
- resolved and pending `.then()` chains;
- native adoption and thenable assimilation;
- Promise combinators;
- async function call/await/resume;
- queueMicrotask/Promise-job enqueue and drain at small/large depths;
- nextTick + microtask mixed checkpoints;
- forced-GC live/dead Promise workloads;
- the complete JS batch, Test262 baseline wall time, and Node regression gate;
  and
- representative DOM/Jube async workloads.

Capture allocation and structural facts:

- bytes charged by first Promise (the old multi-megabyte slab must disappear);
- Promise GC object size/class;
- live/peak/finalized Promise counts;
- reaction and job peak depth/growth counts;
- root-range registration calls attributable to Promise allocation;
- lazy expando frequency; and
- GC trace/finalize time and live-edge counts.

### 13.2 Acceptance

Required:

1. per-Promise root-range registration calls are exactly zero;
2. the root-registration profile bucket attributable to Promise allocation is
   absent/noise (historical target <0.5% of working samples);
3. no multi-megabyte first-Promise slab allocation remains;
4. no cap case drops work or returns a representation-limit failure;
5. aggregate release workloads have no material unexplained regression;
6. ordinary non-Promise object/property and generic HashMap VMap workloads are
   neutral within noise;
7. Promise hot paths do not route state/result/reaction access through generic
   property lookup; and
8. GC work scales with live Promise edges, not allocation high-water or fixed
   capacity.

An apparent regression is rerun and profiled before design changes. Do not
restore the table, cache Promises permanently, bypass precise rooting, or defer
the problem to JR8. `release_profile` may name attribution buckets, but only
optimized `make release` runs decide performance acceptance.

---

## 14. Risks and hard stop conditions

| Risk | Required response |
|---|---|
| VMap trace callback needs allocation or script re-entry | Redesign the carrier edges so tracing is a pure mark walk. D4.3.3 is non-negotiable. |
| Promise raw pointer survives an allocator/uv callback | Convert the owner to rooted Item and reacquire. Never rely on table immortality or non-moving GC alone. |
| Reaction growth can partially publish | Reserve/append atomically through the shared helper; propagate failure. Do not increment a count before ownership exists. |
| Queue growth failure has no error path | Thread an explicit checked/in-band completion to the semantic boundary. Never log and drop. |
| Promise expando needs duplicate descriptor logic | Delegate to an ordinary metadata-qualified Map through `JsPropertyOps`; do not implement a second shape system. |
| Promise vtable is mistaken for a Jube host type | Keep `host_type` null and classify by vtable identity in the semantic resolver. |
| Custom prototype needs a property sentinel | Use the traced override Item plus explicit state flag. No internal spelling. |
| GC object size misses its assumed class | Add static assertions and measure an intentional class/layout. Do not split into an untyped side table. |
| Unhandled tracking appears to need all rejected Promises forever | Recheck checkpoint ownership; retain only candidates until decision. Do not create a permanent strong/weak registry. |
| Job unification changes nextTick priority | Keep two logical lanes over one deque mechanism and lock ordering before migration. |
| Shared async base starts depending on Promise/task semantics | Move policy back to the frontend; core owns Item records, lanes, publication, and lifetime only. |
| Completion publication runs a task/JS continuation inline | Enqueue the named destination lane after terminal-state commit; assert the D6.3.1 checkpoint guard. |
| Tune7 partially migrates `LambdaTask` and leaves two schedulers | Stop at the exact R6a Item API boundary; finish Promise/JS lanes first and leave the Lambda switch wholly to R6b. |
| Cancellation is made symmetric with Promise | Cancel/unsubscribe the task waiter only. Never add cancellation state to Promise or propagate it to unrelated subscribers. |
| Work-budget limit would drop jobs | Leave jobs queued and yield; capacity and fairness are separate policies. |
| Reset sees Items from a retired heap | Fix reset ordering and exact context roots before heap destroy; do not add stale-pointer plausibility checks. |
| A Map-only Promise consumer is ambiguous | Identify whether it wants Promise brand or ordinary Map storage; use the semantic helper only for the former. Do not broadly accept VMap. |
| LOC grows after deletion sweep | Simplify carrier/queue interfaces and finish predecessor deletion before landing. |
| Vendor/C2MIR change appears necessary | Stop and request direction; both are outside Tune7 authority. |

---

## 15. Recommended commit series

A reviewable green sequence is:

1. `test(js): lock Promise jobs reflection and async lifetime behavior`
2. `tool(js): add Tune7 Promise mechanism census`
3. `docs(design): permit precise engine-native VMap carriers`
4. `refactor(js): retain Promise Items in resolver and async state`
5. `test(gc): lock backend-specific VMap trace and finalization`
6. `refactor(runtime): dispatch VMap lifecycle through its vtable`
7. `refactor(runtime): add shared async Item deque and completion port`
8. `refactor(js): define GC-owned Promise carrier and reaction records`
9. `refactor(js): publish Promise native completion subscriptions`
10. `refactor(js): route Promise metadata properties and prototypes through ops`
11. `refactor(js): switch Promise producers to the heap carrier`
12. `refactor(js): migrate Promise consumers and compatibility bridges`
13. `refactor(js): delete Promise table wrapper index and fixed reactions`
14. `test(js): admit unbounded Promise allocation and reaction fixtures`
15. `refactor(js): migrate microtask and nextTick lanes to runtime async deque`
16. `refactor(js): replace unhandled rejection table with checkpoint queue`
17. `test(js): admit growable queue and rejection tracking fixtures`
18. `test(js): ratchet Tune7 representation lifetime and JS-first async kernel`
19. `docs(js): record GC-owned Promise and R6b Lambda handoff`

Commit boundaries may combine where the tree cannot be green between carrier
producer switch and predecessor deletion. Temporary dual-reader commits must
remain inside the Tune7 series, carry exact ratchets, and never be presented as
a completed phase.

---

## 16. Completion checklist

### Formal and design

- [x] D7.4.1v2 adopted and formal-design semver bumped.
- [x] JR7 design/open question updated in the redesign ledger.
- [x] JR7.1 Promise/task unification boundary and JS-first landing order recorded.
- [ ] Implemented docs describe the final carrier/queue.
- [ ] No semantics change is falsely claimed for a representation redesign.

### Promise carrier and identity

- [ ] One GC-owned VMap carrier with one immutable vtable brand.
- [ ] Allocation/reclamation/equality lifecycle pinned by assertions/tests.
- [ ] `host_type`/`host_data` are not used for Promise identity.
- [ ] `js_promise_to_item`, wrapper Map, and `__promise_idx` are gone.
- [ ] Static table/cap/index/reset/free machinery is gone.
- [ ] Promise metadata, prototype, expando, and extensibility behavior pass.

### Reactions and algorithms

- [ ] One growable reaction vector and append owner.
- [ ] All fixed `[8]` arrays, count/cap branches, and dead `is_finally` state are gone.
- [ ] Settlement detaches reactions and preserves FIFO/async behavior.
- [ ] Resolution, adoption, species, combinators, finally, async/await, and TLA pass.
- [ ] Growth failure cannot silently drop a reaction.

### GC and ownership

- [ ] VMap lifecycle dispatch is per vtable and generic HashMap behavior remains green.
- [ ] Promise tracer marks every and only owned Item edge.
- [ ] Raw Promise pointers do not cross MAY_GC without a rooted Item.
- [ ] Async/resolver/module/host state stores Promise Items, not indices.
- [ ] Dead settled and pending Promises are collectible.
- [ ] Sweep/teardown finalization is exactly once.
- [ ] Multi-epoch reset/poisoned forced-GC gates pass.

### Jobs and rejection tracking

- [ ] One core-owned growable deque implementation serves both JS scheduling lanes.
- [ ] Core async headers contain no Promise/JS semantic dependency.
- [ ] Promise publishes rooted native completion subscriptions exactly once.
- [ ] Native subscription delivery is lane-enqueued, never foreign-inline.
- [ ] Promise jobs and queueMicrotask share the microtask lane.
- [ ] nextTick priority and nested ordering are preserved.
- [ ] No fixed-capacity enqueue/drop path remains.
- [ ] Strong unhandled candidates live only through checkpoint decision.
- [ ] unhandledRejection/rejectionHandled/domain/strict behavior passes.
- [ ] Shutdown/reset clears queue owners before heap destruction.
- [ ] R6a bridge tests preserve D6.3.1 Promise/task ordering.
- [ ] Exact R6b observer/closure/runnable-queue deletion handoff is recorded;
      Tune7 does not claim the Lambda-side migration complete.

### Structural, behavior, and performance evidence

- [ ] `utils/js_promise_census.py --check` passes every §11 target.
- [ ] `utils/js_object_census.py --check` has no JR7 sentinel exception.
- [ ] Focused JS, Test262, Node, GC, Jube, and Radiant gates pass.
- [ ] Correct-output cap fixtures are green and admitted.
- [ ] Production C/C++ delta is net-negative.
- [ ] Release A/B and profile evidence satisfy §13.
- [ ] No C2MIR, vendor, generated-Lua, or unrelated layout change landed.

Tune7 is not complete when a VMap merely wraps the old index, when dead
Promises remain in a context list, when reaction/job growth can silently fail,
or when the predecessor code is only unused. Completion means one Promise
identity, normal GC lifetime, growable storage, exact queue ownership, and zero
old mechanism.

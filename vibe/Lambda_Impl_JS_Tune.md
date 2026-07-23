# LambdaJS Performance — Implementation Plan, Tune 6

**Status:** revised draft v4

**Revision date:** 2026-07-23

**Scope:** remaining LambdaJS-specific performance work after the completed
Stack-API/MIR tuning and the Lambda core M1/M2 numeric tuning.

This revision removes work already implemented:

- bounded non-string ADD inference and recursive return-type reinference;
- the HashMap-backed Radiant DOM wrapper identity cache.

It also removes the former native-INT arithmetic proposal. JavaScript has one
numeric type, `Number`, represented by canonical binary64
(`LMD_TYPE_FLOAT`). Lambda's core `int` semantics and M2 lowering do not apply
to JavaScript arithmetic.

The remaining tracks are:

- **Track A** — loop-versioned residency for proven shaped float fields;
- **Track B** — runtime-epoch-owned intrinsic-prototype caching and invalidation;
- **Track C** — listener lookup, DOM dispatch, transpiler allocation, and
  production-build scaling work.

The former shape-based PIC remains outside this plan and is tracked by
`vibe/Lambda_Issues_Outstanding.md` OI-6. Unboxed scalar container storage
remains deferred under OI-9.

---

## 1. Governing invariants

### 1.1 JavaScript has no separate INT value type

JavaScript `Number` is binary64. Ordinary JS numeric literals, arithmetic
results, increments/decrements, bitwise results, and values boxed at an
observable boundary must use the canonical JS Number representation:
`LMD_TYPE_FLOAT`.

`MIR_T_I64` may still be used for implementation-only values such as array
indices, lengths, bitwise intermediates, guards, and Boolean conditions.
A Lambda compact int may also be received at a Lambda/host interop input, but
it must be normalized to the canonical JS Number representation before it
becomes observable as a JS value or enters ordinary JS arithmetic. Neither
case creates a second JavaScript numeric type, and a JS arithmetic result must
never be boxed as a Lambda compact int.

Consequences:

- do not port Lambda M2's `int`-preserving ADD/SUB/MUL lowering into LambdaJS;
- do not let a `both_int` implementation detail choose the observable result
  representation;
- any future integer-ALU strength reduction must be transient, prove exact
  binary64 equivalence, convert the result back to binary64, and demonstrate a
  measured win over the existing double path;
- no such integer-ALU work is committed by this plan.

### 1.2 Representation and lifetime stay on the common Stack API

New lowering must use `MirValue`, `FnVariantAnalysis`, normalized call effects,
and the common `MirEmitter` frame/root/scalar-home machinery. This plan must not
introduce JS-local rooting, scalar homes, raw untracked calls, or a second
representation-conversion system.

### 1.3 Correctness precedes speed

Every fast path retains the current slow path as its semantic fallback.
Prototype mutation, aliasing, getters/setters, Proxies, suspension, exceptions,
GC, runtime/heap reset, and future cross-realm behavior are correctness
boundaries, not benchmark exceptions.

---

## 2. Baseline and acceptance protocol

Before implementing a remaining track, establish a fresh release baseline from
the current tree. The completed JS MIR tuning changed generated structure
substantially, so the measurements that originally ranked Tune 6 are not an
acceptance baseline.

For every landing:

- build and measure with `make release`; debug MIR is structural evidence only;
- run the focused fixtures for the touched subsystem;
- run the LambdaJS GTest suite and `make test262-baseline` with zero failures
  and zero retry-only results;
- run `make test-lambda-baseline` byte-identically because the runtimes share
  `Item`, GC, and the common emitter;
- run the relevant DOM/editor gate for Track C DOM changes;
- run GC-rooting/forced-GC coverage for changes involving Items, calls, cached
  Items, or native side tables;
- run `make node-baseline` on the final candidate for each track.

Release measurements:

- **Track A:** nbody, matmul, mandelbrot, spectralnorm, and a shaped-field
  read/write microbenchmark;
- **Track B:** push-heavy loops, for-of/spread, Array method dispatch, and
  kostya array workloads;
- **Track C1/C2:** many-target listener lookup, DOM/editor wall time, and
  property/method-dispatch microbenchmarks;
- **Track C3:** per-compile allocation/zeroing bytes, compile/JIT time for small
  and large modules, and full Test262 batch wall time;
- **composite check:** richards, deltablue, and the six currently difficult
  macrobenchmarks so local wins do not hide broader regressions.

Record before/after medians and correctness status in this document when a
track lands. Do not infer a runtime win from fewer MIR instructions alone.

Every performance record must also state:

- release revision and build flags;
- benchmark warm-up policy and sample count;
- median plus a dispersion measure;
- the minimum improvement considered material before the run;
- allocation/GC counters when the changed path can trade time for retention.

---

## 3. Track A — loop-versioned shaped-float register residency

### 3.1 Problem

Constructor-typed FLOAT fields already have guarded shaped-slot storage, but a
hot loop can repeatedly call the shaped-slot getter and revalidate the same
receiver, shape, field, and representation. The completed Stack-API tuning
reduced local boxing and conversion traffic; it did not keep an object field in
a native `MIR_T_D` register across repeated accesses.

### 3.2 A0 — eligible-site census

Before implementing loop versioning, instrument or statically count:

- shaped FLOAT reads executed inside loops;
- sites with a loop-invariant receiver and constant constructor field;
- sites rejected by calls, stores, suspension, dynamic dispatch, or aliasing;
- dynamic read counts attributable to the sites that survive all proofs.

Proceed only if the target workloads contain enough eligible dynamic reads to
justify a loop optimizer. Report per-workload counts; nbody is expected to be a
more plausible consumer than array- or scalar-only kernels such as matmul,
mandelbrot, or spectralnorm.

### 3.3 Initial implementation boundary

The first implementation is **read residency only**:

1. identify a loop-invariant receiver and a constant shaped FLOAT field;
2. prove that the receiver and field cannot be mutated through the complete
   loop region, including header/test, body, update, and backedge;
3. clone/version the loop into an optimized loop and the unchanged original
   loop;
4. in the preheader, guard exact expected `TypeMap` identity, initialized
   non-accessor/non-deleted slot state, valid data offset/capacity, and runtime
   `ShapeEntry` type `LMD_TYPE_FLOAT`;
5. on guard success, load the field once into a `MIR_T_D` register and enter
   the optimized loop;
6. on guard failure, enter the original loop, which performs the existing
   property/shaped-slot access on every iteration.

The current structural `js_shape_slot_guard()` is not sufficient for the
resident raw-double path because it does not prove the runtime slot
representation. Structural shape equivalence may remain useful only for the
unchanged boxed/numeric helper path.

Do not coalesce writes in the first landing. Write residency adds externally
observable timing for setters, aliases, calls, exceptions, and suspension and
must be justified by a second profile.

### 3.4 Conservative barriers

Residency ends before:

- an unknown or re-entering helper/user call;
- a call whose normalized effects permit mutation observable through the
  receiver;
- any store through a possibly-aliasing base;
- a store to the same shape/field through another base;
- Proxy/accessor or dynamic-property dispatch;
- loop exit, exceptional exit, `yield`, `await`, or other suspension;
- a shape transition or failed shape guard.

GC alone does not invalidate a raw `MIR_T_D`, but calls that may GC still use the
common emitter and may also be mutation/re-entry barriers. Do not encode
GC-safety as a substitute for the alias/effect proof. Do not retain a raw
`Map::data` pointer across a call or collection; guard and load immediately in
the preheader, then retain only the native double.

### 3.5 Required fixtures

- repeated reads from a stable constructor-shaped float field;
- guard failure enters an unmodified loop whose getter is observed once per
  iteration, not once per loop;
- same name/offset but a non-FLOAT runtime slot;
- structurally equivalent but non-identical `TypeMap`;
- aliased mutation in the middle of the loop;
- same-field write through a second reference;
- getter/Proxy replacement after warmup;
- an unknown user call that mutates the receiver;
- exception and loop exit paths;
- generator/async suspension inside the loop;
- forced GC while the native double is live.

Exit: A0 demonstrates material eligible traffic, followed by a measured
shaped-field load reduction and release runtime improvement with zero
correctness regressions. If A0 finds little traffic, or read residency does not
move the target workloads, stop Track A and do not proceed to write coalescing.

---

## 4. Track B — runtime-epoch intrinsic-prototype fast paths

### 4.1 Current starting point

The runtime already has:

- `js_intrinsic_proto_cache[JS_CLASS__COUNT]`;
- constructor/prototype snapshot and reset machinery;
- `g_array_proto_push_ever_set` and
  `g_array_sym_iter_ever_set` in `JsRuntimeState`;
- a guarded direct `Array.prototype.push` fast path.

Do not add a parallel cache. This track must consolidate and extend the existing
state while making its ownership and reset contract explicit.

LambdaJS currently has one active `JsRuntimeState`; `$262.createRealm()` does
not create independent constructor/intrinsic sets, and cross-realm Test262
coverage is intentionally outside the current product scope. Therefore Tune 6
must not claim or depend on true concurrent realm isolation. For this track,
“owner” means the active JS runtime plus its heap/document epoch. A genuine
per-realm intrinsic model is a separate prerequisite project.

Hot residual costs include:

1. `Array.prototype[Symbol.iterator]` resolving the constructor and probing the
   prototype after the tamper flag is set;
2. the generic Array writable-method probe before virtual builtin fallback;
3. repeated construction/interning of `"prototype"` and constructor names at
   hot prototype-resolution sites.

The current census still finds 45 direct
`heap_create_name("prototype", 9)` sites across `js_runtime.cpp` and
`js_globals.cpp`; the implementation must classify them rather than converting
all sites blindly.

### 4.2 Implementation stages

**B0 — census and ownership**

- classify each prototype lookup as hot-convert, cold-leave, or construction
  only;
- define one `JsIntrinsicState` record owned/referenced by the active
  `JsRuntimeState`;
- move or wrap the existing intrinsic cache, resolving flags, Array tamper
  state, and cached well-known-symbol/name Items in that record;
- document cache rooting and teardown;
- pair the record with `heap_epoch` or an equivalent non-reused owner identity;
- document how constructor snapshot restore, batch reset, crash recovery, and
  document/heap replacement restore pristine state.

**B1 — centralized invalidation**

- route intrinsic-prototype mutations through one invalidation hook used by
  assignment, `defineProperty`, delete, `Reflect`, descriptor, and Proxy paths;
- cover Array push, Array `Symbol.iterator`, and the generic writable-method
  family first;
- retain today's full lookup/probe as the tampered fallback.

Invalidation versions must not create an ABA match across reset. Either keep
versions monotonic for the lifetime of the owner or compare both owner epoch and
per-intrinsic version. Generated MIR must load the active intrinsic state; it
must not embed a stale prototype Item or state pointer that can survive its
owner.

**B2 — hot lookup conversion**

- consume the existing intrinsic prototype cache without re-interning names;
- replace only census-confirmed hot checks;
- measure each converted family independently.

### 4.3 Required fixtures

- fresh document/heap epoch prototype identity and tamper isolation;
- mutation through every supported property-definition/deletion entry point;
- tamper, call, restore/reset, and call again;
- custom Array methods and custom `Symbol.iterator`;
- Proxy/descriptor mutation paths;
- constructor/prototype replacement;
- batch reset and snapshot restore.

Exit: no stale prototype/state use across document or heap epochs, no missed
mutation path, and a measured reduction in prototype/name-pool work on the
target workloads. True simultaneous two-realm isolation is not an exit
criterion for Tune 6.

---

## 5. Track C — runtime and compile-time scaling

### C1. Event-listener target index

`js_dom_events.cpp` still linearly scans `_entries[]` for every distinct target.
Add a `lib/hashmap.h` index from the existing target key to its integer slot in
`_entries[]`. The slot is stable even if the backing array relocates; do not
cache a `NodeListenerEntry*`.

Preserve the current listener arrays, registration order, listener tombstones,
`DomNodeRef` validation, native DOM pinning, document/window sentinels, and
reset behavior. The hash table is an index over the existing listener store,
not a replacement listener store.

The current plain-object key is a raw GC-managed container pointer while only
the callback and signal are rooted. A dead target can therefore leave a stale
key that aliases a later allocation at the same address. For the first landing,
make this lifetime explicit and conservative:

- give each plain-object target entry a stable GC root slot containing the
  target Item;
- create/retain that root for targets introduced by either
  `addEventListener` or an `on<type>` handler;
- release all target roots during event-system reset;
- accept and measure document-lifetime target retention in this landing;
- defer weak-key reclamation to separate GC work.

`_handler_slots[]` is a second linear lookup surface. Add a separate
`(target-key, event-type) -> handler-slot` HashMap index while retaining the
array as the ordering/ownership store. Do not cache `EventHandlerSlot*`.

Gate with many-target/many-listener lookup, detached-node cleanup, object
EventTargets, `on<type>` lookup, event ordering, mutation during dispatch,
forced GC with allocator reuse pressure, target-root teardown, retained-target
memory, and the DOM/editor suites.

### C2. DOM property and method dispatch

Release builds already compile out `log_debug` and `log_info`; deleting or
re-gating those calls is not a release performance task.

The remaining candidate is the large `strcmp` property/method dispatch
surface. Profile it first. If material:

- assign interned/name IDs once and switch or table-dispatch on the ID;
- share generated Jube/Radiant interface metadata where it is authoritative;
- retain explicit ordered fallbacks for expandos, attributes, aliases,
  document/window special cases, and unknown properties;
- do not reorder observable getter, Proxy, or fallback behavior merely to
  shorten the ladder.

### C3. Per-compile transpiler storage and collection limits

#### C3a. Outer function/class collections

`JsMirTranspiler` still embeds:

- `JsFuncCollected func_entries[32768]`;
- `JsClassEntry class_entries[4096]`.

Every compile allocates and zeroes the full structure, and overflow silently
stops collection after logging. Replace these fixed arrays with storage sized
to the source and make overflow an explicit compile error.

Pointer stability is load-bearing: class entries, method entries,
`owner_class`, `current_fc`, and generated-function references retain pointers
into these collections. A growable contiguous buffer must not relocate after
such pointers are published.

Use one shared AST traversal shape for counting and collection. In particular,
class methods are collected specially and must not be missed by a generic
function-node counter. Prefer a count/fill visitor or shared callback-based
walker over two independently maintained eligibility implementations.

Allocation failure, count/index overflow, or a count/fill mismatch must abort
the compile cleanly. A lazy-zero or reuse-only patch is insufficient because it
leaves the silent caps in place.

#### C3b. Per-class collections

Removing the outer arrays does not remove the remaining fixed per-class
metadata limits:

- 128 methods;
- 16 static fields;
- 32 instance fields;
- 8 static blocks;
- 16 constructor-shaped fields.

Replace correctness-bearing method/field/static-block collections with
pointer-stable storage sized by the same count pass, or explicitly detect and
compile-error before collection. Constructor-shaped-field capacity is an
optimization limit: exceeding it may disable constructor-shape optimization
and use the semantic generic path, but must not silently drop fields.

C3a and C3b may land separately, but Tune 6 must not claim that silent
collection caps are eliminated until both are complete.

### C4. Test262-only production gating

The sys-function registry honors `JS_TEST262_FAST_PATHS`, but matching MIR
recognition/emission sites are not gated. Add one compile-time build flag
sourced through `build_lambda_config.json` and visible to helper definitions,
the registry, and LambdaJS recognition/lowering. Prefer one shared helper
catalog over parallel hand-maintained lists. Then verify:

- the normal Test262 build retains all helpers;
- `JS_TEST262_FAST_PATHS=0` compiles, links, and contains no references to the
  gated helpers;
- ordinary JS and Node behavior is unchanged.

This is binary/build hygiene. Do not claim a runtime speedup unless release
measurements show one.

---

## 6. Recommended sequencing

1. **Phase 0:** current release baseline and targeted profiles, including the
   Track A eligible-site census.
2. **C3a:** eliminate large per-compile outer-array zeroing and outer caps.
3. **C3b:** eliminate or explicitly deopt/error on per-class metadata caps.
4. **C1:** add listener/handler indices and the plain-target lifetime contract.
5. **C4:** independent production-build cleanup.
6. **B:** establish runtime-epoch ownership/invalidation, then convert measured
   hot prototype checks.
7. **C2:** only if property/method dispatch is measured hot.
8. **A:** only if A0 justified it; land read-only loop versioning, with write
   coalescing as a separate go/no-go decision.

Re-run the fixed composite after B, C2, and A because all three can affect
array-, object-, and call-heavy workloads.

---

## 7. Open design decisions for discussion

These are the decisions that should be settled before implementation. Routine
census work, fixtures, and mechanical use of `lib/hashmap.h` do not need a
separate design discussion.

### OD1 — Track A field scope

Should the first residency implementation accept only constructor-shaped FLOAT
fields, or also shape-inferred object-literal fields?

**Recommendation:** constructor-shaped FLOAT fields only. They already carry
the strongest storage/type pledge. Use the measurement to decide whether
object-literal promotion belongs in this plan or the deferred OI-9 storage
design.

### OD2 — Track A write coalescing

Should the first implementation retain modified fields and spill them at region
boundaries?

**Recommendation:** no. Land and measure read residency first. Write
coalescing changes mutation visibility across aliases, calls, exceptions, and
suspension and deserves a separate acceptance step.

### OD3 — Track B runtime-epoch owner and reset contract

What concrete runtime object owns intrinsic prototype Items, pristine/version
state, and cached well-known-symbol slots for the active runtime/heap epoch?
How do document setup, batch reset, crash recovery, and prototype snapshot
restore select/reset that owner?

**Recommendation:** define one explicit `JsIntrinsicState` record referenced by
the active `JsRuntimeState`. Move or wrap the existing intrinsic cache and
Array tamper flags there, and associate it with a non-reused `heap_epoch`.
Do not rely on an unqualified file-static cache.

True independent `$262.createRealm()` intrinsics are outside Tune 6. If that
scope changes, stop Track B and design the realm model first.

### OD4 — Track B invalidation granularity

Choose between:

- one monotonic pristine bit per intrinsic prototype;
- per-method-family bits;
- a per-intrinsic mutation version checked by cached fast paths.

**Recommendation:** per-intrinsic version plus named pristine bits only for
exceptionally hot families such as Array push/iterator. The reset/snapshot path
must restore pristine content without allowing an old `(owner, version)` pair
to match new state. This avoids one unrelated prototype mutation permanently
disabling every fast path while keeping hot checks compact.

### OD5 — Track C3 stable collection storage

Choose a storage shape that does not invalidate published
`JsFuncCollected*`/`JsClassEntry*` pointers:

1. pre-count AST functions/classes, allocate exact contiguous arrays, then run
   the existing collection pass;
2. use stable chunks and replace pointer subtraction/indexing with explicit
   indices;
3. migrate all retained cross-links to indices, then use growable contiguous
   arrays.

**Recommendation:** option 1 using a shared count/fill traversal, followed by
exact per-class member allocation for C3b. Use option 3 if exact counting cannot
share the collection traversal without duplicating eligibility rules. Avoid
chunking unless measurement shows that the extra indirection is preferable to
the index migration.

### OD6 — Track C2 dispatch authority

Should DOM name dispatch remain hand-owned in `js_dom.cpp`, or should
authoritative host members be generated from the Jube/Radiant interface table?

**Recommendation:** use generated interface metadata for branded native host
members and keep a small explicit compatibility layer for document/window,
attributes, expandos, and legacy aliases. Settle this before creating another
hand-maintained dispatch table.

---

## 8. Explicitly out of scope

- a separate JavaScript INT type or int-preserving arithmetic result;
- unboxed scalar storage in maps/arrays (OI-9);
- shape-based PIC and duplicate-class-name deopt (OI-6);
- destination-passing lowering;
- GC de-pinning;
- relocatable MIR artifact caching;
- true concurrent/multiple JavaScript realms;
- weak-key listener-table reclamation;
- C2MIR support.

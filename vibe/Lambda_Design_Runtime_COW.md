# Lambda Design: Copy-on-Write for Mutable Value Semantics

- **Status:** rev 8, 2026-08-29. **Stage 1 is LANDED** (`vibe/impl/Lambda_Impl_Tune_COW (done).md`;
  formal-spec conformance: "COW Stage 1 landed" — `let`-finality real for
  Array/Map/Object/Element/VMap). The main body now carries only the **latest
  design**; the rev-1–7 decision narrative, the pre-Stage-1 motivating
  measurements, the Layer-2 options deliberation, the clone-context
  retirement rationale, and the completed Stage-1 phase plan are preserved in
  **Appendix C**. Rev 8 also records the **general-path-simple / COW-as-
  special-case** simplifications: CW30 (compile-gated snapshot iteration),
  CW31 (exclusivity faces 2/4 without region math), CW32 (ArrayNum COW by
  compile-time path selection), and extends CW29 (whose landing dissolves
  NM-O8 and supplies the NM-O2 idiom).
- **Implementation state at a glance:** see §0.
- **Owns:** the engineering realization of C4's copy discipline — uniqueness
  tracking, the copy path, retirement of the eager clone anchor, and the
  related value-sharing/interning rules.
- **Semantic authority:** `doc/Lambda_Formal_Semantics.md` §9 (C4 — mutable
  value semantics). This document may not change what a program observes
  (P6: sharing must be unobservable); every mechanism here is implementation
  freedom under that contract.
- **Companion designs:** nested mutation `vibe/Lambda_Design_Nested_Mutation.md`
  (CW22–CW28, extends this doc's CW series); model survey
  `vibe/Lambda_Design_Memory_Model.md` §2.4 (Perceus), §5–6 (composition).
- **Convention:** `file:line` refs drift; confirm against symbol names.

---

## 0. Current state (2026-08-29)

| Area | Ruling(s) | State |
|---|---|---|
| Stage-1 core-container COW (Array/Map/Object/Element/VMap) | CW3, CW6–CW14, CW19, CW21 | **LANDED** — eager clone anchor retired (CW10); `_cow` wrapper family live; counters (CW5) live |
| S9.3.1 insertion capture | formal spec S9.3.1 | **UNCONDITIONAL — escape hatch retired 2026-08-29** (env no longer consulted; gated arms and `is_proc_param` deleted) |
| Nested mutation: CW24 mutated-place-copy error, CW25 path borrows | CW22–CW28 (companion doc) | **Implemented on worktree branch `nm-impl-work` (unmerged)**; 9 corpus scripts migrated |
| S9.1.3 plain-param snapshots | **CW29** (§11.9) | **DEFAULT ON — flipped 2026-08-29.** The 88-script sweep ceiling collapsed to **13 real reliance sites**, all migrated to `var` (goldens unchanged). Place-copy binds now mark (true S9.1.2 snapshots); CW24v2 phase 2 live (observed copies legal; dead-store shape warns) |
| Snapshot iteration (S9.2.3) | **CW30** (§11.6) | **Implemented 2026-08-29** (worktree `nm-impl-work`, pending merge): compile-gated, tier-shared decision; fixture `cow_snapshot_iteration.ls` |
| Exclusivity faces | §11.3, **CW31** | Faces 1+3 **landed** (`E211`, whole-base). Face 2 unreachable behind `E229` (callee-side design recorded). Face 4 **implemented 2026-08-29** (worktree `nm-impl-work`, pending merge): `NameEntry::view_base` + effective-root compare; fixture `var_view_overlap.ls` |
| View-state `var` rule | S9.2.4v2, §11.4 | Not implemented (teaching error pending) |
| View-borrow confinement | CW16.3, §11.7 | Not started |
| ArrayNum COW | CW15–CW16, **CW32v2** (§11.8) | **IMPLEMENTED 2026-08-29** (worktree `nm-impl-work`): mark-only binds, guarded lane stores on marked roots (byte-test at offset 4 → cold detach + republish), `array_num_set_cow_idx`/`index_assign_cow` wrappers, T0 mask fix. Fixture `cow_arraynum_alias.ls`. Mutable views OPEN/TODO by designer ruling |
| JS↔Lambda ownership boundary | CW17, §9.3 | **Deferred by designer (2026-08-28)** |

## 1. Context A — the C4 semantic contract this must implement

C4 (§9): **values never alias; `var` is the only mutability marker and the
`var` param the only sharing construct; `let` is final.** Binding, assignment,
and construction copy *observably* — implementation is COW on sharing that
must stay unobservable (P6). Cycles are unconstructible (§9.3), so `==` is
total and **no cycle collector is ever needed for Lambda values**. Isolates
share nothing, so **no uniqueness state needs atomicity**.

What the semantics *obligates* the engineering to deliver:

1. **In-place hot path for unique values** — `push`/`splice`/index-assign on
   an unshared container must not copy (§9.1 rule 3 note: `var`-param borrows
   exist to preserve exactly this).
2. **O(depth), not O(size), copies for shared documents** — §9.5.1 resolves
   direction: structural sharing (spine copy, subtree reuse), Clojure/immer
   precedent.
3. **The naive deep-update spelling must not silently copy subtrees** —
   §9.5.2. Stage 1 defined how each copied child is written back into its
   copied parent (owner writeback, §6); the surface ergonomics are owned by
   the nested-mutation design.

**The rev-8 design principle, stated once:** Lambda is functional, so by
default there is **nothing to do** — the general path (calls, loops, reads,
stores) emits no COW work at all. COW is the special case, entered only where
a mutation provably exists, and the *decision* of who pays is made at compile
time; the runtime pays at most one bit. CW29–CW32 are this principle applied
to each remaining item.

## 2. Context B — motivating measurements (RETIRED)

> Stage 1 landed; the eager clone anchor and its measured wall (Result10) are
> gone. The pre-Stage-1 analysis is preserved in **Appendix C.2**. Current
> performance records live in `test/benchmark/Overall_Result3x` and the Tune
> ledgers.

## 3. The design question and the three layers

Every mutation entry point must answer one question: **"am I the sole owner
of this value right now?"** Unique → mutate in place; shared → copy first
(then mutate the copy, which is unique by construction). The three layers
below are three ways to answer it — or avoid asking:

| Layer | Mechanism | Answers the question… | Cost when it applies |
|---|---|---|---|
| 1 | Static uniqueness (JIT) | at compile time — question never asked at runtime | zero |
| 2 | Dynamic uniqueness signal | at runtime, O(1) | one bit-test + branch |
| 3 | Cheap copy path | doesn't answer it — makes the "shared" answer affordable | O(width) per level, lazy |

## 4. Layer 1 — static uniqueness (Perceus-lite in the MIR transpiler)

`Lambda_Design_Memory_Model.md` §2.4 ranks Perceus-style inferred uniqueness
as the most interesting language-level option for Lambda; this layer is its
first, deliberately modest slice:

- **CW1 — `var`-param borrows stay signal-free.** A `var` param is an
  exclusive borrow (§9.1 rule 3, compiler-checked): mutation through it needs
  no bit test and no copy, ever. Already the contract; Layer 2 must not tax it.
- **CW2 — provably-fresh values skip the signal.** A container the JIT can
  prove uniquely owned at the mutation — freshly built literal not yet
  stored/passed, result of a copy the transpiler itself just emitted,
  last-use reuse — mutates in place with no runtime check.
- **Caveat recorded:** Koka/Lean/Roc run this analysis over full static
  types; Lambda is dynamically typed and the JIT's inference is partial, so
  Layer 1 fires *less* here than in the precedents. **Layer 2 is
  load-bearing in Lambda to a degree it is not in Koka** — which is why the
  dynamic signal, not the static analysis, is this document's center.
- **Rev-8 extension:** the transpiler's *own* compile-time share knowledge
  (`MirVarEntry::cow_marked` and the CW29/CW30 body walks) is itself Layer-1
  machinery: it decides at emission time which bindings ever need the Layer-2
  test, so unmarked bindings keep fully raw stores. CW32 (§11.8) leans on
  exactly this for ArrayNum.

## 5. Layer 2 — the dynamic uniqueness signal

### 5.1 Options considered (RETIRED)

> The full options table — full refcount, saturating count, 1-bit flag,
> GC-refreshed signal, always-copy, pure persistent structures — and the two
> asymmetries that framed the choice are preserved in **Appendix C.3**. The
> decision stands below.

### 5.2 Decision

- **CW3 (DECIDED, designer, 2026-07-23): v1 ships the 1-bit shared flag.**
  Keep it simple first: no decrement protocol, no header growth, monotonic
  semantics that are trivially auditable. Accepted cost: post-sharing copies
  that a counting scheme would avoid. (A false "shared" is only a wasted
  copy; a false "unique" is a semantic bug — every default errs toward
  shared.)
- **CW3-C (noted 2026-07-26, from concurrency K32):** the same `is_shared`
  bit becomes the **cross-isolate** discriminator. Tier-2 thread workers
  share deeply-immutable Items *by pointer* across isolates
  (`Lambda_Design_Concurrency.md` §10.3.3), and the rule there is that
  refcount/COW operations are atomic **only** for objects carrying this bit —
  biased refcounting, so single-threaded code and unshared data pay nothing.
  Two consequences land back on this design: (1) the bit's monotonic,
  never-cleared semantics are now a *feature* (a shared-across-threads object
  can never be un-shared behind a peer's back); (2) **closing the C4.1
  aliasing catalog becomes a prerequisite for thread-mode sharing** — an
  interior alias captured before sharing writes into a subgraph another
  isolate is reading, which is a correctness bug in one isolate and a data
  race across two.
- **CW4 (DECIDED, designer, 2026-07-23): the switch to a small saturating
  count — and a GC-refreshed signal — are deferred** pending deep
  benchmarking on a corpus of live/production Lambda scripts, not synthetic
  benchmarks alone. Both upgrades are bit-compatible extensions of the 1-bit
  flag; nothing in v1 forecloses them.
- **CW5 — instrumentation ships with v1 (LANDED).** Counters (per TypeId):
  shared-bit sets, mutations on unique (in-place), mutations on shared
  (copies taken), bytes copied. Release-safe. These counters *are* CW4's
  future benchmarking data. Mutation counters are entered only through Lambda
  `_cow` wrappers/cold helpers; raw JS/in-place stores incur no counter
  branch.

### 5.3 Bit placement and the flag's exact semantics

**CW6 — bit 0 of `Container.cow_state`, not `Container.flags` and not the GC
header.** Header housekeeping has reserved byte 4 for COW without changing
the public eight-byte `Container` size or any derived-container offset:
`flags` remains byte 1, `array_flags` byte 2, `map_kind` byte 3,
`cow_state` byte 4, constructor-reserved mask bytes 5–6, and
`reserved_state` byte 7. The formerly macro-only JS-property and
constructor-reserved flags are proper `has_js_props` / `has_ctor_reserved`
bitfields occupying bits 6/7 of `flags`; they are not available to COW.

`COW_STATE_SHARED = 1u << 0`. The remaining `cow_state` bits reserve the
zero-layout-growth path for CW4's future small count or GC-refreshed state.
The byte exists uniformly for heap and arena-owned containers; the GC header
does not.

**State semantics:** `cow_state & COW_STATE_SHARED` means *may be reachable
by more than one owner*. New runtime containers start at zero. A share mark
is monotonic and idempotent. A one-level copy starts unique (its shared bit
is cleared); the source remains shared. Static/arena-owned containers are
treated as shared. Copy helpers must initialize the destination state
explicitly rather than blindly copying `cow_state`. Over-approximation is
legal; under-approximation is a bug.

**Set points (share events)** — all are existing centralized entry points:

1. Binding/assignment of a container without copying (`cow_bind_var`): set
   the bit on the value; its children are *not* touched — see Layer 3 for
   why that suffices.
2. Storing a container into a container (§9.3 construction capture) when the
   copy is deferred: literal build, field/index write, `push`/`splice`
   argument (`cow_capture_value`, gated per S9.3.1).
3. Returning/capturing a Lambda value or otherwise installing a second
   Lambda-visible owner.
4. *(CW29, designed)* the callee prologue of a `pn` that mutates a plain
   container parameter (§11.9).
5. *(CW30, designed)* the head of a loop whose body mutates the looped
   collection's root (§11.6).

JS↔Lambda boundary ownership is deliberately absent from this set and is
specified for Stage 2 in §9.3.

**Statically-owned data is born shared (CW8).** `is_static` / `is_immortal`
/ `!is_heap` containers (parser-built Mark data in the Input arena, const
pools) are treated as permanently shared — first mutation copies into the
runtime heap (materialization), the old migration pattern made lazy and
universal. `MarkBuilder` sets `COW_STATE_SHARED` at construction; no
ownership range-test is ever needed.

**Test points:** every interior-mutation entry (index/field assign,
`push`/`splice`/`pop`, `pn`-method receiver mutation, MarkEditor edits):
`owner = cow_prepare_write(owner); mutate(owner);`. The replacement must be
installed into the binding or copied parent slot before mutation. The JIT
inlines the state test + branch on hot paths (Layer 1 elides it where
uniqueness is proven); the cold copy helper returns the replacement.

### 5.4 CW21 — raw mutation stays raw; Lambda COW is a wrapper layer

**DECIDED (designer, 2026-07-23); LANDED.** Existing in-place APIs keep
their current semantics and signatures. `array_set()`, `fn_array_set()`,
`fn_map_set()`, `vmap_set()`, JS array/property/shape setters, and raw
push/splice/pop helpers do not inspect or update `cow_state`, do not copy,
and do not acquire Lambda ownership policy. LambdaJS depends on these
operations having reference semantics.

The Lambda-only `_cow` family (`array_set_cow`, `member_set_cow`,
`map_set_cow`, `vmap_set_cow`, `array_push_cow`, …) delegates at the highest
existing raw layer that already owns bounds checks, representation
conversion, error behavior, and storage details. Each `_cow` API has one
contract:

1. precisely root `owner`, `value`, and any live parent chain;
2. call `cow_prepare_write(owner)` and receive the original unique owner or a
   one-level replacement;
3. mark a stored container value shared when the operation creates another
   Lambda owner, unless the JIT proves a true move/last use;
4. delegate the actual mutation to the existing in-place API;
5. return the possibly replaced owner for mandatory installation into its
   binding or parent slot.

APIs that also return a semantic result, such as `pop`, return the updated
owner and place the operation result in an out parameter; they never hide
owner replacement in a local pointer. Errors use the existing runtime error
channel and must not make the caller lose the current owner.

The JIT may inline the unique test and raw store, but that lowering is an
optimization of the `_cow` contract. JS lowering never emits `_cow`. No
language-mode test is added to `array_set()` or another raw hot path.

## 6. Layer 3 — the copy path: one shallow level + mark children shared

**CW9 — a COW copy is one level deep: copy the container's own storage
(header + slot/field array), then set `COW_STATE_SHARED` on every *container*
child, sharing them by pointer.** Scalars copy by value (or are immutable
pointers — §8). Cost O(width of one level), never O(subtree).

This single rule *is* §9.5.1's structural sharing, without inventing a new
node representation: the existing containers become the persistent structure.

- **Spine copying requires explicit writeback.** Mutating `t.nodes[i].value`
  on a fully-shared `t` copies `t` (marking `nodes` shared), then `nodes`
  (marking its elements shared), then `nodes[i]` — the spine, O(depth) levels
  of O(width) — and installs each new child into the new parent. Every
  untouched subtree remains shared. Later mutations on the now-unique spine
  are in-place. The owner chain is lowered as MIR registers/stack state,
  never a heap path descriptor.
- **Transactional child marks are cleared when the pre-image is unique.**
  A one-level clone marks the source's children shared; when the source was
  unique and is about to be replaced at its only binding, those marks are an
  artifact of the transaction, not real sharing, and under the monotonic flag
  they would cost every later child write a spurious detach. Landed fix; the
  probe is the typed-map aliased-write fixture.
- **DAG shape is preserved for free.** Lazy COW never deep-copies, so shared
  substructure simply *stays* shared; divergence happens per-written-path
  only. (Observably identical either way — values have no identity.)
- **Stage-1 kinds:** `Array`, `Map`, `Object`, `Element`, and `VMap`.
  ArrayNum remains on its current specialized behavior until CW32 lands; no
  generic COW test, Item walk, or visited map on its native path.
- **VMap is a backend contract, not a header copy (DECIDED, LANDED).**
  `VMapVtable` carries a one-level snapshot/detach operation, consulted only
  by `vmap_set_cow()`. On the Lambda shared cold path, the HashMap backend
  clones its table and insertion-order storage in one pass and marks
  container keys and values shared. Task handles stay immutable; a
  host-backed VMap enters Lambda COW only when its visible backing is
  snapshot-stable, else `vmap_set_cow()` rejects without calling the host
  setter.
- **Map/Object shape metadata is shared, not copied per value.** `TypeMap` /
  `ShapeEntry` are immutable once shared by Lambda COW values.
  `map_set_cow()` detaches/transitions Lambda shape metadata before
  delegating to a raw update that may change `ShapeEntry::type`. JS raw shape
  mutation and its JS-specific detach rules are unchanged.
- **Undo/history falls out** (§9.5.1): a retained old root is a full snapshot
  at O(spine) cost — the editor gets persistence as a side effect.

## 7. The clone context: retired (DONE)

> **CW10 landed.** For Stage-1 kinds the binding anchor is: set
> `COW_STATE_SHARED` on the RHS value and bind the pointer — O(1), no clone,
> no visited hashmap. `fn_mutable_value`'s generic deep-clone body and the
> anchor exemptions are gone from the binding/assignment path (ArrayNum keeps
> a narrow eager-clone compatibility path in `cow_bind_var` until CW32).
> What legitimately remains of deep copy: true ownership-transfer boundaries
> that leave the value heap — isolate messages (K13), exports materializing
> outside Item space. Retirement rationale and migration notes: **Appendix
> C.4**.

## 8. Co-design: value sharing and interning (strings first)

The complement of COW: **immutable values never need it.** COW machinery
applies only to the Stage-1 mutable kinds listed in §6; everything else shares
unconditionally because sharing an immutable value is unobservable:

- **CW11 — strings, symbols, binary, decimal, datetime share by pointer on
  every copy, no bit, no clone.** `String` has no mutation API and must never
  grow one; string "mutation" (`fn_strcat` etc.) always builds a new value.
- **CW12 — interning status quo is kept; no global hash-consing.** Names and
  symbols ≤32 chars intern in the NamePool (pointer-equality fast path per
  A6); map keys/field names intern via the shape/name machinery. A global
  runtime string table is **rejected for now**: it taxes every creation to
  speed repeated equality. Revisit only on CW5-counter evidence.
- **CW13 — targeted caches, evidence-gated:** empty-string singleton,
  single-char string table, small-int→string cache. None lands without a
  counter showing the allocation actually recurs.
- **CW14 — const-literal hoisting composes with CW8:** an all-constant
  container literal can be built once (arena/const pool, born shared)
  and share-and-marked per evaluation instead of rebuilt.

## 9. Stage-2 design: ArrayNum, mutable views, and JS↔Lambda interop

Designer ruling (2026-07-23): ArrayNum views serve **two languages**. Under
Lambda they follow C4 — *"ArrayNum should behave exactly the same as Array
from the user's perspective."* Under JS they follow JS semantics (TypedArray
views alias their ArrayBuffer by spec). **CW15–CW18 are design requirements.**
ArrayNum is performance-critical native storage; a naive generic COW
treatment would defeat its purpose — CW32 (§11.8) is the strategy that
avoids it.

### 9.1 CW15 — the layering (DECIDED, designer, 2026-07-23)

**One shared storage layer; two thin per-language policy layers; no language
semantics inside the storage layer.** `JsTypedArray` *already rides ArrayNum*
— `ta->view` is an `ArrayNum` and its shape side-table an `ArrayNumShape`,
with `JsArrayBuffer` layered on top for buffer identity/detach/resize.

| Layer | Contents | Owner |
|---|---|---|
| **Storage (shared)** | elem-kind enum + load/store dispatch, `ArrayNumShape` stride/shape math, slicing, view descriptor (`is_view`/`is_mutable_view`, base + `extra` shape), SIMD kernels, memcpy copy, bounds checks, growth | one implementation, language-free |
| **Lambda policy** | the shared bit in `cow_state` + the CW32 compile-time path selection; view/borrow rules of CW16 | Lambda runtime + MIR transpiler |
| **JS policy** | `JsArrayBuffer` identity, multi-wrapper aliasing, detach, resize/length-tracking, species — per ECMA spec | `js_typed_array.cpp` |

### 9.2 CW16 — Lambda policy: representation invariance + borrow-only write-through

1. **Representation invariance.** ArrayNum is an optimization of
   representation, not a semantic type: it participates in COW identically to
   Array (shared-state test; copy = one `memcpy`, no children to mark) and must
   be observably indistinguishable from Array — equality, `type()`/`is`,
   iteration, mutation semantics. **Consequence:** ArrayNum `==`
   representation-sensitivity is *by this ruling a bug*. **Verified FIXED
   2026-08-29** (probes: ArrayNum vs push-built generic, cross-rank int/float,
   view vs base, 2-D matrix vs nested generic, row view, NaN rows — all
   conform); the Typed-Array-4-era defect was closed by the 2026-07-16 OI-1
   operator-surface pass. Remaining obligation: pin these probes as a
   conformance fixture before CW32 ships, since detaches change
   representations behind programs' backs.
2. **Read views stay first-class values — COW makes them correct for free.**
   A read view (`is_view && !is_mutable_view`) is a zero-copy slice: create
   it, mark base and view shared. Snapshot semantics falls out of Layer 2:
   whichever holder mutates first COW-copies its storage and the other keeps
   the old bytes — observably a copy at creation (P6), costing nothing until
   divergence.
3. **Write-through (mutable) views are borrows, never values.** Under C4 a
   first-class write-through view is a reference cell — banned. The construct
   survives as the **array form of the path-shaped `var` borrow**: legal only
   in `var`-param / receiver position, exclusivity-checked, non-escaping (not
   bindable, returnable, or storable). Same `is_mutable_view` machinery
   underneath; confinement is static (**CW16.3**).
4. **Borrow requires unique storage (un-share at borrow).** Creating a
   mutable borrow over shared storage first COW-copies the base at the
   borrow root — otherwise write-through would mutate bytes value-holders
   snapshotted. Swift's inout-uniqueness is the precedent. After the one-time
   un-share, the borrow writes raw (CW1: no per-write cost).
5. **Migration.** Today's mutable views are first-class bindables (Scope-3
   image toolkit, procedural in-place updates). Most call sites already use
   them in `pn` in/out roles, so the change is largely signature
   formalization; `proc_view_mutable` and the image-toolkit suite are the
   fixtures.

### 9.3 CW17 — JS policy: reference semantics preserved; detach at the boundary

> **Deferred by designer, 2026-08-28** — recorded for when the boundary work
> is picked up. Within JS, TypedArray/ArrayBuffer aliasing is untouched and
> **JS-owned buffers never consult Lambda `cow_state`** — JS hot stores stay
> branch-free. The two languages meet only at the boundary:

- **Ingress (JS → Lambda):** a shared mark alone is insufficient if the
  Lambda wrapper still points at bytes JS can mutate. Ordinary
  `ArrayBuffer`/TypedArray ingress must either transfer/detach ownership or
  copy into Lambda-owned storage. `SharedArrayBuffer` ingress always copies.
- **Egress (Lambda → JS), writable view requested:** crossing is a share
  event. **Detach-at-wrap:** if the ArrayNum is shared, copy once into a
  JS-owned buffer at wrapper creation. Lambda holders keep their snapshot;
  every JS alias derives from the one buffer made at that crossing. The copy
  is boundary-grained (one memcpy per crossing), never store-grained.
- Read-only egress may share storage only under an explicit
  non-retaining/non-writing contract.

### 9.4 CW18 — the sharing discipline (what must not fork)

Everything in the storage row of §9.1 exists **once**. The only per-language
code is the Lambda bit-test/borrow rules and the JS buffer-identity layer.
Standing guard (CLAUDE rule 13): any near-duplicate arising between
`js_typed_array.cpp` and `lambda-vector.cpp`/`lambda-data-runtime.cpp` gets
hoisted into the storage layer, never copied.

## 10. Stage 1 — implementation record (DONE)

**CW19 (two-stage delivery) executed.** Stage 1 — Lambda-native COW for
`Array`, `Map`, `Object`, `Element`, `VMap`, with no exclusivity enforcement
— is **landed** (`vibe/impl/Lambda_Impl_Tune_COW (done).md`). The phase plan
(P0–P6) and its gates are preserved in **Appendix C.5**. Three parts of §10
remain **binding contracts**, not history:

1. **The unique-mutation fast path is non-negotiable:** one byte
   load/test/branch, no helper call, allocation, child scan, or path-object
   allocation. Known scalar RHS emits nothing; known container sharing emits
   an inline OR; only dynamically typed values may call a helper. Existing
   raw C/JS mutators gain no COW branch, helper call, share mark, or change
   of aliasing behavior — ever.
2. **Ordering obligations (fixture-pinned):** (a) value arguments capture
   their snapshot **before** any borrow's un-share/raw-write begins (C4.2c);
   (b) an assignment whose RHS borrows the target's root (`a[i] = g(var a)`)
   resolves the store address **after** RHS evaluation — un-share-at-borrow
   may move the storage.
3. **Rooting:** all allocating copy paths use precise `RootFrame` / `Rooted`
   ownership for source, replacement, incoming value, and owner chain, and
   reload moved pointers after a possible GC. Forced-GC stress is a standing
   gate; the `let g`/`var h` probe is the false-unique canary.

P6 — evaluating the CW4 saturating-count / GC-refresh upgrade against CW5
production-corpus counters — remains open by design.

## 11. Stage 2 — exclusivity & borrow discipline

**CW20 — the complete check design**, revised at rev 8 by CW29–CW32 under
the §1 principle: every check is compile-time; the only runtime cost is a
bit set in the construct that mutates.

### 11.1 Why the check exists

The `var` borrow is the one deliberate hole C4 punches in "values never
alias": for the duration of a call, two names refer to one storage.
Exclusivity keeps that hole single-writer. It is the precondition for four
things: the callee's local reasoning (its params are independent values);
**CW1's zero-cost raw borrows** (no shared-state consult — sound only if no
other observer exists during the borrow); the JIT's no-alias optimization
freedom (every `var` param is `restrict` by construction); and the §9.6
formal reading (each `var` an independent `x′ = f(x)` sequence). Without the
check, `f(x, x)` silently reintroduces aliasing *at the call site, invisible
in the callee* — and the double-borrow case has no good outcome under
un-share-at-borrow: not copying aliases the writers; copying detaches the
second borrow into a dropped duplicate (silent lost updates). Hence: reject
at compile time.

### 11.2 The structural fact that bounds the design

Borrows live only for the duration of a call, and the runtime is
single-threaded — so while a borrow is alive, the only code running is the
callee and its callees. A second live writer can therefore arise from exactly
**two** sources: (i) another borrow born at the same call site; (ii) a name
the callee reaches independently — view-state `var`s (§11.4; the
module-level case is vacuous under S9.1.7). Everything else is sequential
with the borrow by construction. This is what keeps the check local and
cheap — no lifetime inference, no whole-program analysis.

### 11.3 The call-site check — one overlap predicate, four faces (CW31 revises faces 2 and 4)

Enumerate the call's `var` arguments (the `pn`-method receiver counts as
one), test pairwise overlap:

| Face | Overlap to reject | Example | State (2026-08-29) |
|---|---|---|---|
| 1 — two+ `var` args | same variable | `f(x, x)` | **Landed** (`E211`) |
| 2 — receiver vs `var` args | receiver is a `var` param | `list.append_all(list)` | **Unreachable** — dynamic dispatch of `var`-param `pn`s is itself deferred (`E229`), so no such call compiles; see CW31 |
| 3 — path borrows | **path-prefix** relation: `x` vs `x.f` conflict | `f(var t, var t.nodes[i])` | **Landed** at whole-base granularity: same base ⇒ conflict, so `f(var t.a, var t.b)` is also rejected — the ladder's sanctioned v1 false positive |
| 4 — mutable view borrows | same **base**, per CW31 v1 | `f(var view(img,r1), var view(img,r2))` | **Implemented 2026-08-29** (`nm-impl-work`): `NameEntry::view_base` recorded at `var v = subview(base,…)` bindings, effective-root compare in the call check; fixture `var_view_overlap.ls` |

**CW31 (PROPOSED, 2026-08-29) — faces 2 and 4 without new machinery.**

- **Face 4 v1 needs no region math.** When resolving each `var` argument's
  root binding, look *through* `subview`/view expressions to the base
  binding, then apply face 3's existing same-root conflict. Compile-time
  only, zero runtime state, and the disjoint-tiles false positive is the
  same one §11.7 already documents as intentional for face 3. The
  granularity ladder below is unchanged — region-precise checking remains a
  future upgrade, decided on real image-toolkit code.
- **Face 2 stays unimplemented until `E229` lifts, and then goes
  callee-side.** A static caller-side check cannot see a dynamic callee's
  param kinds — but the callee knows its own (the CW29 placement). When
  dynamic dispatch of `var`-param functions lands, a callee with two or more
  `var` params (receiver included) performs one pointer-identity/overlap
  check in its prologue; every other call pays nothing. Static call sites
  keep the caller-side face-1/3/4 checks, which subsume face 2 there.
  Implementing anything sooner is dead code.

**Granularity ladder** (start coarse, refine on demand): v1 = whole-base
conservative (same base ⇒ conflict; sound, rejects some safe programs).
Upgrades, cheapest first: (a) **disjointness-by-construction splitters** — a
blessed `split(var arr, at)` / `rows(var img)` / `tiles(var img, n)` builtin
takes *one* borrow of the base and returns provably-disjoint mutable views in
a single operation (Rust `split_at_mut` precedent); likely sufficient for the
image toolkit with no new analysis; (b) static range-disjointness for
constant indices; (c) Swift-style dynamic bookkeeping — record
`(base, byte-range)` per active borrow, check overlap at borrow creation —
only for dynamic ranges, only if (a)/(b) prove insufficient.

### 11.4 The one non-local case: ~~module-level and~~ view-state `var`s

> **Narrowed 2026-08-28 (designer): the module-level half is ruled out by
> design, not deferred.** `var` is a procedural binding, and a module-level one
> is rejected outright with `error[E224]`; S9.1.7 states the general law
> ("Lambda script has no global mutable state"). **View state is the live
> case** — a real mutable binding living outside any `pn`, which a callee can
> still reach independently of the borrow. Ratified as **S9.2.4v2**, naming
> view state alone.

```lambda
// Illustrative only — this shape does not parse (E224).
var g_list = [...]
pn push_sorted(var list: int[]) { ... g_list ... }  // callee also names the global
push_sorted(g_list)                                  // two write paths, one storage
```

No call-site inspection can see this — it depends on what the callee
transitively touches. Opening rule: **forbid passing view-state `var`s as
`var` arguments** — compile error with a teaching message ("name the view
var directly in the `pn`, or copy in/out"), matching the C4.2a start-strict
philosophy. A dynamic borrowed-bit (Swift's Law-of-Exclusivity dynamic arm)
remains the recorded fallback if the strict rule proves too tight; callee
effect summaries are rejected as whole-program analysis.

### 11.5 What needs no check — sequential by construction (do not re-add guards)

- **Borrow in argument position** — `f(var a, g(var a))`: `g`'s borrow
  completes during argument evaluation, before `f`'s borrow activates.
- **Re-borrow / recursion** — `pn f(var a) { g(var a) }`, recursive `f`: a
  single writer chain; one live writer at any instant.
- **Readers** — writer-vs-writer only (C4.2c): plain params snapshot by
  value (CW29 is the implementation); no read bookkeeping, ever.
- **Nested-`pn` up-level access (C4.2a relaxation)** — the enclosing body is
  suspended while a callee holds its borrow, so the nested `pn` cannot run
  concurrently with it; the non-local route is already §11.4's case.

### 11.6 CW30 — snapshot iteration, compile-gated (IMPLEMENTED 2026-08-29 on `nm-impl-work`; revises the earlier head-mark sketch)

**The ruling (S9.2.3, ratified):** `for (x in arr) { arr[2] = 99 }` — the
loop walks the entry-time value; the first in-body mutation COW-copies. No
iterator-invalidation UB. Same rule for pipes over `var` containers.
**Implemented 2026-08-29** exactly as designed below (worktree branch
`nm-impl-work`): `AstLoopNode::snapshot_collection` computed once in
`build_ast.cpp` at for-node completion via the shared
`ast_body_may_write_entry` walk (ast.hpp); T0 = one conditional mark in
`interp_for_level`; MIR = mark + independent rooted handle at the three
`emit_box(collection)` sites. Fixture:
`test/lambda/proc/cow_snapshot_iteration.ls`, identical on both tiers.

**Why the first implementation attempt failed, and what it teaches.** An
unconditional share-mark at every loop head was tried and reverted: in MIR,
the loop's boxed-collection register *is the binding's own register* when the
source is a bare name, so publishing the detached replacement into the
binding clobbers exactly what the loop is walking. That fight only existed
because the mark was emitted into the **general** loop lowering.

**The rev-8 design — the general loop emits nothing.** At compile time, gate
on two facts, both already computable:

1. the loop source is a name (or path) rooted at a **mutable** binding — a
   `var` or `var` param. A fresh source (`for x in f()`) has no second
   observer; a `let` source cannot be written at all. Both skip everything.
2. the loop **body may write that root** — direct assignment, a path write
   rooted at it, or passing it as a `var` argument (the same conservative
   body walk as CW29; a spurious hit costs a copy, never a lost write).

Only when both hold does the loop become the special case: share-mark the
collection at the head **and copy its boxed Item into a dedicated rooted
slot; the loop walks the slot**. The binding's register is then free to take
detached replacements — the register-aliasing blocker disappears by
construction, because the snapshot handle is independent of the binding. T0
mirrors with a conditional one-line mark in `interp_for_level` (the
unconditional version is already verified working).

Non-mutating loops — the functional default — are untouched on both tiers:
no mark, no extra slot, no reload.

### 11.7 Stage-2 acceptance

The C4.4 compile checks (`var`-args-only; exclusivity per §11.3 including
the CW31 subview-base resolution; `var` receiver for `pn` methods) + CW16.3
view-borrow confinement + the §11.4 view-state rule + CW29 plain-param
snapshots + CW30 snapshot iteration + fixtures: `f(x,x)`, `x.merge(x)`,
view-state-borrow overlap, disjoint-tiles rejection under the conservative
rule (documents the intentional false positive), snapshot-iteration goldens,
and the CW29 SI3v2 tier-parity fixtures.

### 11.8 ArrayNum and JS↔Lambda boundary work

Stage 2 also owns the implementation of §9. JS↔Lambda boundary work (CW17)
is **deferred** (designer, 2026-08-28); Stage-1 test262/Node runs remain
regression gates only.

**CW32v2 (RATIFIED and IMPLEMENTED 2026-08-29, worktree `nm-impl-work`) —
ArrayNum COW by the shared bit, checked at operation entry; unshared arrays
pay nothing.** Supersedes
the rev-8 v1 sketch ("never consult the runtime flag on the fast path"),
which was over-strict: the SIMD objection applies to a *per-lane-store*
check, not to the bit itself.

**The ruling — same Layer-2 discipline as every other container:**

1. **Unshared → in-place, no COW.** An ArrayNum that never crossed a
   sharing boundary writes raw, exactly as today. This is the functional
   default and covers the benchmark kernels (locally built, never aliased
   before writing).
2. **Shared → one `cow_prepare_write` at the mutation ENTRY, then raw.**
   The check is hoisted to the operation, never the lane store: a kernel or
   fill does one bit-test + (if shared) one `memcpy` detach at its head,
   and every subsequent store in the operation is raw. Sound because the
   bit is monotonic and sharing only begins at boundaries (binding,
   capture, param, loop head) — none occur mid-operation. A one-off
   element store pays one predictable branch, which is just the checked
   path it already has.
3. **`cow_bind_var`'s eager ArrayNum clone retires** for plain (non-view)
   ArrayNum — binding an alias becomes O(1) mark-and-share; the first
   COW-aware write detaches. ArrayNum's detach is the cheapest of any
   container: one packed `memcpy`, no children to mark.
4. **Compile-time `cow_marked` demotes to pure Layer-1 elision** — an
   unmarked binding's stores keep the raw fast path with no entry check at
   all; a marked binding's stores route through the flag-consulting path.
   Correctness comes from the bit; speed from the analysis.
5. **Read views**: mark base and view shared at creation — CW16.2 snapshot
   semantics falls out (whichever side mutates first detaches; the other
   keeps the old bytes).
6. **Mutable (write-through) views — OPEN/TODO by designer ruling
   2026-08-29.** They retain their current first-class write-through
   behavior and are *not* marked shared — a mark would make the view's
   first write detach and silently stop reaching the base, breaking the
   construct's purpose. A mutable view is a borrow, not a share; its
   discipline (un-share-at-borrow CW16.4 + confinement CW16.3 +
   `cow_bind_var` keeps the eager clone for view *aliases*) lands with the
   view-borrow work, tracked in B.1. Until then mutable-view aliasing is
   accepted Stage-2 residue, unchanged from today.
7. **Equality**: representation invariance is **verified fixed** (CW16.1
   note above); the obligation is a pinned conformance fixture, since
   detaches flip representations behind programs' backs.
8. **JS unchanged**: JS-owned buffers never consult the bit (CW17,
   deferred).

**Recorded upgrade, evidence-gated:** for a *marked* binding written in a
hot loop, hoist the one prepare above the loop (the CW30 body walk already
identifies loop-written roots). Ship v1 without it — marked-and-hot is the
rare case, and CW5 counters will show whether it ever matters.

**Implementation record (2026-08-29, `nm-impl-work`).** Runtime:
`cow_bind_var`'s ArrayNum arm is now mark-only (views returned untouched —
same as the old clone's view arm, so view behavior is unchanged);
`array_num_set_cow_idx` (machine-index lane-store entry) and
`index_assign_cow` (mask writes) added as CW21 wrappers that prepare,
delegate raw, and return the owner for mandatory republication. MIR: one
`emit_array_num_cow_guard` (byte-test of `Container.cow_state` at offset 4,
static-asserted) emitted ONLY when the destination root is `cow_marked` /
`cow_children_may_be_shared`, wired into the int/float/bool fast arms and
the boxed default arm, all funneling into the shared cold fallback, which
gained the detach-and-republish arm (including
`mir_rebind_typed_array_layout` — a detach moves storage, so any cached
layout is stale). T0: the mask-assign site's stale "already detached"
assumption replaced with the wrapper + republish. Non-binding destination
objects keep today's raw behavior (pre-existing residue, unchanged).
Verified: 7-shape probe `test/lambda/proc/cow_arraynum_alias.ls` (alias both
directions, float lane, `let`/`var` canary, subview write-through, unaliased
loop, aliased mask write) — byte-identical across tiers and observably
identical to the old eager-clone semantics (P6), with the bind now O(1).

### 11.9 CW29 — S9.1.3 plain-param snapshots: callee-side mark (IMPLEMENTED behind the gate, 2026-08-29)

§11.5's "plain params snapshot by value" is currently a ruling without an
implementation: a container argument crosses the call boundary with
`cow_state` untouched, so a callee write through a plain `pn` parameter
mutates the caller's value in place. Worse, the typed checked-write tier
*deliberately* selects the in-place setter for plain `pn` params
(`is_var_param || is_proc_param`, six sites across both tiers) — a flag
minted to make T0 match MIR when both implemented write-through. The formal
spec's S9.1.3 conformance row records the annotation as "documentation
rather than a gate".

**The ruling — nothing happens until a mutation exists.** Lambda is
functional; a call performs no snapshot work. The entire mechanism is:

1. **Callee prologue, mutated params only.** At compile time, walk the `pn`
   body for writes through each plain container parameter (the same shape of
   body scan `has_elem_type_invalidation` already performs). For each such
   parameter, emit one `cow_mark_shared(param)` at entry. A non-mutating
   callee — the functional default — emits nothing, and no call site
   anywhere changes. A mutating callee sets one bit; the first write hits
   the existing `cow_prepare_write`, detaches a private copy, and every
   later write is in-place on that copy. No new runtime functions.
2. **Retire `is_proc_param` from setter selection.** The "detached candidate
   republished to the callee's own binding" behavior — recorded as the T0
   bug that `is_proc_param` was invented to suppress — is *exactly* the
   S9.1.3 semantics: visible inside the procedure, invisible to the caller.
   The fix is deleting the flag from the six `is_var_param ||
   is_proc_param` sites, after which both tiers agree by construction
   (they fall into the same detaching path). The flag and its plumbing
   retire with it.
3. **`var` params untouched** — already correct (detach-at-borrow via
   `mir_prepare_cow_root`, then in-place).

**Rejected alternative — call-site marking.** An earlier sketch marked each
container argument at the call site, gated on the S9.3.1
named-vs-fresh-argument predicate, with separate reasoning for dynamic
dispatch. All of that machinery buys only one thing: a fresh argument
(`f([1,2,3])`) to a *mutating* callee avoids one detach on first write.
That is a copy the value semantics permits, not a correctness issue —
revisit only on benchmark evidence. The callee-side mark travels with the
callee, so every call form (including dynamic dispatch) is covered for
free.

**Known cost.** Under CW3's monotonic 1-bit flag, the caller's original is
also marked by the callee's prologue (same container), so the caller's next
write after the call detaches spuriously. Same cost `let` aliasing already
pays; watch the benchmark rows for calls into mutating callees. NM-O6's
written-once witness is the eventual answer if it matters.

**Two issues CW29 resolves for free (2026-08-29):**

- **NM-O8's typed arm dissolves.** NM-O8 records a flat-vs-nested
  inconsistency: nested path writes through a plain param stayed local while
  flat writes published. Under CW29 *staying local is correct for both* —
  retiring `is_proc_param` makes flat writes join the nested behavior, the
  two arms agree by construction, and the untyped-arm root-skip fix
  (`cow_path_set_inplace` on plain params) gets **reverted** — it patched
  toward the wrong semantics. An open issue closed by deletion.
- **NM-O2 gains its idiom — and is now CLOSED (CW24v2, 2026-08-29).** After
  CW29, a plain `pn` parameter *is* a private mutable snapshot. The designer
  subsequently ratified the Swift/R endpoint: CW24's error is a migration
  guard, retired for the general case after the flip migration; at steady
  state the binding itself is the deliberate copy, so no `copy()` builtin is
  minted. See the nested-mutation doc §4.3 and its Appendix C survey.

**Rollout — steps 1–2 executed 2026-08-29 (worktree `nm-impl-work`).**

1. **Sweep done.** `LAMBDA_COW_PARAM_NOTE=1` + `--emit-ast-dump` over every
   `pn`-defining corpus script: **88 of 413 scripts flagged, 746 note
   lines** — a conservative ceiling (the pass-to-a-`pn` arm counts chains
   whose leaf may not write). Benchmarks dominate: `crypto_rsa` 110,
   `havlak`/`havlak2` 52 each, `richards` family 35/21, `navier_stokes` 30,
   `json`/`json2` 22. This is a far larger migration than S9.3.1's four
   scripts; the flip decision must price it. Notes list:
   `temp/cw29_sweep_notes.txt`; sweep: `temp/cw29_sweep.sh`.
2. **Semantics landed behind `LAMBDA_COW_CAPTURE`** (default OFF, same gate
   as insertion capture — the two halves flip together): the FUNCTION_END
   walk stores `NameEntry::cow_param_mutated` (rebinds excluded); both tiers
   mark at activation entry (T0 slot bind; MIR generic-Item prologue); the
   six `is_var_param || is_proc_param` selections and the NM-O8 root-skip
   gate down to `is_var_param || (is_proc_param && !cow_capture_enabled())`.
   Probes (tier parity exact): flag OFF `9 9 9 5` (write-through ABI
   pinned in `test/lambda/proc/cow_param_write_through.ls`), flag ON
   `1 2 a 5` (flat, nested-path, and array writes all local); the four
   LR12-9 S9.3.1 probes still return the ruled `1` with both halves on.
   **Scope note (narrowed once CW32v2 landed):** flagged
   ArrayNum-through-plain-param now snapshots (the prologue mark composes
   with the CW32 guard; probed both tiers). The residual write-through under
   the flag is only the declared typed-array *native-witness* path, whose
   raw pointer feeds a native body before any guarded store.
3. **FLIP EXECUTED 2026-08-29; ESCAPE HATCH RETIRED same day after the
   baseline soaked.** `LAMBDA_COW_CAPTURE` is no longer consulted anywhere:
   `cow_capture_enabled()` is deleted, the six write-through selections read
   `is_var_param` alone, and `is_proc_param` is gone from NameEntry,
   MirVarEntry, and both setters. The retirement also fixed a borrow-side
   gate: `mir_root_may_need_cow` was a container allow-list defaulting to
   FALSE, so a binding carrying a composite declared type (`var xs: any[]`
   holds the open-any-array TypeUnary id) skipped un-share-at-borrow and a
   `var` callee wrote through the caller's alias. The gate is now a
   scalar-exclusion defaulting to TRUE — an elision over the no-op-when-
   unshared `cow_prepare_write`, per the CW32v2 principle. The 88-script ceiling collapsed
   to **13 real reliance sites** — 7 write-through-pinning proc tests and 6
   benchmarks (the SOM PRNG/out-param idiom: bounce, permute, storage,
   towers, mbrot2, cd2_orig) — all migrated to `var`, goldens unchanged;
   `let st` holders became `var st` where borrowed. The write-through fixture
   became `cow_param_snapshot.ls`, pinning the shipped semantics. Two pieces
   landed WITH the flip: **place-copy binds mark their value** on both tiers
   (`var row = m.rows[i]` is a true S9.1.2 snapshot; T0 in the
   `interp_bind_declared_value` funnel, MIR in the decl lowering), and
   **CW24v2 phase 2** (observed mutated copies are legal; only the
   dead-store shape warns — `cow-dead-snapshot`).

## 12. Settled decisions and residual risks

### 12.1 Settled implementation contract

These are accepted decisions, not open design questions:

1. **Raw/COW API separation (DECIDED, LANDED).** Existing in-place mutators
   remain policy-free and unchanged for JS/host callers. Lambda mutation goes
   through replacement-returning `_cow` wrappers. JS lowering never calls
   `_cow`.
2. **Mutation ownership/writeback ABI (DECIDED, LANDED).** Every mutation
   lowering owns an assignable root/parent slot. `cow_prepare_write(Item)`
   returns the replacement; nested writes propagate and install replacements
   along the owner chain, kept in MIR registers/stack state, never a heap
   descriptor.
3. **VMap snapshot/detach contract (DECIDED, LANDED).** `VMapVtable` hook,
   used only by `vmap_set_cow()` on its shared cold path; `_cow` rejects
   non-snapshot-capable host/capability VMaps before raw delegation.
4. **Performance accounting (DECIDED).** No blanket percentage cutoff; the
   Result9/10-protocol release measurement, with unique-mutation,
   scalar-heavy, allocation-heavy, and editor/document rows inspected
   separately so a headline geomean cannot hide a hot-path regression.
5. **Shape metadata invariant (DECIDED, LANDED).** `map_set_cow()`
   detaches/transitions a shared shape before calling the raw updater.
6. **GC rooting of a copied spine (DECIDED, LANDED).** Precise rooting,
   reload after possible compaction, forced-GC coverage mandatory.
7. **Rev-8 addition — compile-time COW decisions (CW29–CW32 pattern).** Who
   pays for COW is decided by the emitter (mutation body-walks, `cow_marked`
   tracking), never by adding a runtime test to a general path. A
   conservative walk may cost a spurious copy; it must never cost a lost
   write or a general-path branch.

### 12.2 Deferred questions and residual risks

1. **Stage-2 granularity endpoint** (ladder in §11.3): does the image
   toolkit need more than splitters — do dynamic `(base, range)` checks ever
   pay their way? Decide on real toolkit code.
2. **`Element`/document depth:** Layer-3 spine cost is O(depth×width); deep
   narrow documents are fine, shallow enormous fan-out nodes (10⁵-child
   element) pay O(width) per copy of that node — the §9.5.1
   node-representation question (chunked children) stays open for exactly
   this case; the editor benchmark decides (NM-O1).
3. **CW29/CW30 spurious-copy pressure:** the monotonic flag plus
   conservative body walks can mark bindings that a precise analysis would
   not. CW5 counters + the benchmark rows for mutating callees and mutating
   loops are the watch; NM-O6's written-once witness is the recorded remedy
   if evidence demands one.

---

## Appendix A — Fully-typed Lambda vs. Go and Rust: the performance ceiling

> **Parking note:** this analysis lives here because the question was raised
> while designing COW ("with this COW design, what can a fully statically
> typed Lambda script reach?"). It should **move to an overall performance
> design doc** when one exists — it is engine-wide, not COW-specific.
> Written 2026-07-23.

**The question.** If a Lambda script is fully statically typed (all types
concrete, inference complete), with the C4 + COW machinery of this document
in place — what performance class does it reach? Go? Rust? Better?

### A.1 Three determinants

**1. The codegen substrate is nearly Rust-class already.** The vendored MIR's
own benchmark table (`ref/mir/README.md`, "Current C2MIR Performance Data")
puts MIR-generated code at **0.91× of gcc -O2 (geomean)** across its 15-small-
C-benchmark suite — clang -O2 sits at 1.09 in the same table. So on straight
scalar code the substrate is within ~10–20 % of the LLVM league. Honest
caveat: small kernels flatter it — MIR has **no auto-vectorization** and few
loop transforms (no unroll-and-jam, limited fusion), so gcc/LLVM pull further
ahead on complex loops. The conclusion that matters: **the ceiling is not set
by MIR; it is set by what our lowering makes MIR chew on.**

**2. What full typing + this COW design eliminates** — essentially every tax
separating today's Lambda from a static language:

- no boxing: self-tagged doubles and int56 stay in native registers end-to-end;
- no anchors: CW10 deleted the eager clone from binding/assignment;
- COW bit-tests mostly gone: Layer 1's dynamic-typing caveat (§4) *inverts* —
  with concrete types, Perceus-style uniqueness elision fires the way it does
  in Koka/Lean, so hot-loop mutations are raw in-place stores;
- borrows write raw (CW1), scalars never allocate (side number stack), errors
  are values (no unwinding, no per-call exception polling);
- and a genuine edge over Go: **no GC write barriers** — Go pays one on every
  heap pointer store; Lambda's non-generational collector charges the mutator
  nothing between collections.

**3. What remains, versus each rival:** bounds checks (Go-like; less
elimination initially — BCE is an open item); GC safepoint publication (zero
in scalar-only loops, stores only at may-GC calls); container headers; the
call protocol (fib at 6.7× Node in Result10 shows this gap is real — R4-class
work in `Lambda_Tuning_Proposal.md`); and a non-generational stop-the-world
collector on allocation-heavy code (R7).

### A.2 Verdict by workload class

| Workload (fully typed) | Landing zone | Why |
|---|---|---|
| scalar / numeric hot loops | **Go-class — ceiling slightly above Go** | substrate 0.91× gcc -O2; Go's own compiler is typically 1.1–2× behind gcc -O2 on tight loops (weaker optimizer + bounds checks) |
| array / vector kernels via ArrayNum builtins | **beats Go; approaches Rust** | Go does not auto-vectorize; Lambda's ArrayNum kernels are hand-SIMD; bandwidth-bound ops tie everyone |
| allocation-churn (trees, graphs, builders) | **behind Go** until R7 generational GC + matured Layer-1 reuse | Go's concurrent GC is excellent; with FBIP-style reuse, Koka's evidence says functional-update workloads can reach parity |
| across the board vs Rust | **no** | the gap is structural — LLVM optimization depth, zero GC, layout control, monomorphization — not a flaw in the COW design; parity only in the kernel pockets |

### A.3 Calibration against today

Result10's already-typed cluster (tak 1.05× Node, mbrot 1.12×) is V8-class
now. The path from there to the ceiling is exactly the work already on the
books: this COW design (anchors gone), R4 call-protocol slimming, R6
MIR-Direct specialization parity, R7 GC generational work — plus
bounds-check elimination as a new item not yet in any ledger.

### A.4 Bottom line

> Close to Go: **yes — that is the design ceiling for general fully-typed
> code**, with real beat-Go pockets (SIMD kernels, barrier-free mutation,
> reuse-friendly workloads). Close to Rust: **only in those kernel pockets**.
> Better than Rust overall: **no** — the reasons are LLVM-depth and
> GC-existence, neither of which COW touches. For a JIT-compiled scripting
> language with a GC and value semantics, "Go-class with Rust-adjacent
> kernels" is about as high as the ceiling goes — and nothing in the C4+COW
> semantics itself stands between here and there.

---

## Appendix B — C4 residue: outstanding items NOT implemented by Stage 1 (Tune-COW)

> Companion to `vibe/impl/Lambda_Impl_Tune_COW (done).md` §0 (whose ledger
> marks these OUT). **This list is what remains open of C4** — the checklist
> for declaring C4 fully done. Recorded 2026-07-23; states refreshed
> 2026-08-29 (see also §0).

### B.1 Owned by Stage 2 of this document (§11)

| Item | Source | Pick-up trigger / state |
|---|---|---|
| **ArrayNum COW and view policy** — native packed-copy strategy, representation-invariant equality, read-view snapshots, mutable-view borrows | §9.1–§9.2; §11.8 (CW32v2) | CW32v2 ratified 2026-08-29 (shared-bit at operation entry); equality verified fixed; **mutable-view borrows stay OPEN/TODO (designer ruling 2026-08-29)** — current write-through behavior retained until CW16.3/16.4 land |
| **JS↔Lambda ownership boundaries** — mutable-buffer ingress/egress, `SharedArrayBuffer`, explicit read-only sharing | §9.3; §11.8 | **DEFERRED by designer 2026-08-28**; Stage-1 JS suites are regression-only |
| Exclusivity checks — four call-site faces + the `var`-args-only check | C4.4 #1; §11.3 (CW31) | Faces 1+3 **landed** (`E211`, whole-base). Face 2 unreachable (`E229`); callee-side design recorded. Face 4 live hole; v1 = subview-base resolution |
| View-state `var` passed as `var` arg (the non-local overlap) | §11.4 | Module-level half **CLOSED** — vacuous by design (S9.1.7/E224). View-state half: forbid with a teaching error (S9.2.4v2) |
| View-borrow **confinement** (mutable views become non-escaping, `var`-position-only) | CW16.3; §11.7 | Stage 2; until then mutable views retain current behavior |
| Snapshot iteration over a mutated `var` container (S9.2.3) | §11.6 (CW30) | Ruled but **violated both tiers**; compile-gated design recorded 2026-08-29; record as C4.2d when implemented |
| **Plain-param snapshots (S9.1.3)** — callee-prologue share-mark on mutated plain container params; retire `is_proc_param` | §11.9 (CW29) | Designed 2026-08-29; flips together with S9.3.1 under `LAMBDA_COW_CAPTURE`; diagnostic corpus sweep first. Dissolves NM-O8; supplies NM-O2's idiom |
| Exclusivity granularity endpoint (splitters vs static ranges vs dynamic checks) | §11.3 ladder; §12.2 | decide on real image-toolkit code during Stage 2 |

### B.2 Needs its own design first (no owner document yet)

| Item | Source | Notes |
|---|---|---|
| **Nested-mutation ergonomics** — path-shaped `var` borrows (`f(var t.nodes[i])`), `_modify`-style in-place accessors, guaranteed get-modify-put | C4.4 #6; `doc/Lambda_Formal_Semantics.md` §9.5.2 | **OWNED as of 2026-08-28 by [`Lambda_Design_Nested_Mutation.md`](Lambda_Design_Nested_Mutation.md) (CW22–CW28)**, which extends this doc's CW series. CW24 (mutated-place-copy error) + CW25 (path borrows) are **implemented on worktree branch `nm-impl-work`**; nine corpus scripts migrated |
| **Element/document node representation for huge fan-out** — chunked children so a one-level copy of a 10⁵-child node isn't O(width) | §9.5.1 residue; §12.2 | gate on the editor/document benchmark; only if it fails on real documents (NM-O1) |
| **Non-escaping nested-`pn` relaxation** (direct up-level `var` access for call-position-only nested `pn`s — the closure-style parser case) | C4.2a spec sketch | backward-compatible addition; interim idiom (object with `pn` methods) is unblocked by Tune-COW Phase B |

**Nested mutation gates the S9.3.1 flip (2026-08-28).** Insertion capture is
implemented on both tiers behind `LAMBDA_COW_CAPTURE` (default off;
[LR12-9](Lambda_Issue_Ledger.md#lr12-9)). It cannot become the default while
element/field reads still borrow: as soon as a slot holds a captured value,
the get-modify idiom `c = owner[i]` … `c[j] = v` writes a detached copy and
loses the update. Closing insertion without closing reads breaks the one
spelling every nested-container script uses; measured flip cost is four
corpus scripts. **What actually gates the flip is CW24 alone** — it converts
the silent lost update into located, mechanical fixes; CW25/CW26 remove a
verbosity tax and can follow. CW29 (plain-param snapshots) flips in the same
gate. The path-borrow aliasing that CW25 fixes was a standing violation of
ratified **S9.2.2** ("a mutable borrow over shared storage un-shares
first"), so CW25 is conformance work rather than new semantics.

### B.3 Different project

| Item | Source | Where it lives |
|---|---|---|
| Formal model: `⟨store⟩` cell only for `var` bindings; `let`-finality and COW-unobservability as *verifiable properties*; the `let g`/`var h` probe as a model-level regression fixture | C4.4 #5; §9.6 | Stage-4 semantics/DSL work (`vibe/Lambda_Semantics_DSL_Proposal.md` lineage), not an engine change |

### B.4 COW-side deferral riding the same timeline (not a C4 item)

CW4 — the saturating-count / GC-refreshed-signal upgrade to the 1-bit flag —
is COW-internal, listed here only for completeness: its trigger is the CW5
counter corpus from live/production scripts, gathered *through* Stage 1
operation, decided per §5.2.

---

## Appendix C — History and retired designs

> Moved out of the main body at rev 8 (2026-08-29): the body carries only
> the latest design; nothing here is load-bearing for new work, but the
> rulings' provenance and the reasoning behind rejected alternatives are
> preserved for audit.

### C.1 Revision history

- **rev 1 (2026-07-23):** initial proposal; Layer-2 v1 mechanism decided by
  designer — the 1-bit shared flag (CW3), simplicity first; the
  saturating-count upgrade deferred behind production-corpus benchmarking
  (CW4).
- **rev 2:** added §9 (CW15–CW18), the dual-language ArrayNum/mutable-view
  policy, per designer ruling 2026-07-23 — one storage layer, Lambda follows
  C4, JS follows JS semantics, maximal shared implementation.
- **rev 3 (CW19, designer 2026-07-23):** two-stage delivery — Stage 1 basic
  COW with NO exclusivity enforcement, performance first; Stage 2 records
  the full exclusivity/borrow-discipline design now, implemented later.
- **rev 4:** narrowed Stage 1 to Lambda-native `Array`, `Map`, `Object`,
  `Element`, `VMap`; ArrayNum COW and JS↔Lambda interop moved to Stage 2 as
  representation-specific, performance-sensitive designs.
- **rev 5:** accepted the remaining Stage-1 decisions — VMap mandatory
  backend snapshot/detach hook; nested mutation via replacement-returning
  owner writeback; Map/Object shapes shared only under
  detach-before-mutate; copied spines precisely rooted. Performance ruled to
  have no arbitrary per-benchmark cutoff: Result11 must be materially better
  than Result10 and target Result9-class or better.
- **rev 6 (CW21, designer 2026-07-23):** isolated language policy in
  Lambda-only `_cow` mutation wrappers; existing in-place APIs unchanged for
  LambdaJS/host code.
- **rev 7 (CW29, 2026-08-29):** added §11.9 — S9.1.3 plain-param snapshots
  via a single callee-prologue `cow_mark_shared` on mutated plain container
  params; `is_proc_param` retired from checked-setter selection.
- **rev 8 (2026-08-29):** Stage 1 recorded as LANDED; body restructured to
  latest-design-only with history in this appendix; CW30–CW32 added under
  the general-path-simple principle; CW29 extended with the NM-O8
  dissolution and the NM-O2 idiom.

### C.2 Pre-Stage-1 motivating measurements (Result10, 2026-07-22)

`test/benchmark/Overall_Result10.md` (revised in place after the R0a fix):
MIR dedup geo **4.26× (R9) → 8.42× vs Node** (1.75× worse like-for-like, 52
common rows); LambdaJS **13.1× → 16.7×** like-for-like. Three findings drove
this design:

- **The eager clone anchor was a measured wall.** The transpiler wrapped
  container-possible RHS of `var` decls and assignments in
  `fn_mutable_value()` — an **eager deep clone** with a per-call
  visited-hashmap (`MutableCloneContext`). collatz executed it ~40–50 M
  times *on scalars* (hashmap create/destroy per int): part of its 7.47 s
  wall. `Lambda_Impl_Tune3.md` M3 diagnosed it and was put ON HOLD —
  "designer may go straight to refcount COW instead of patching the
  eager-clone anchor." This design was that path. (M3's remedies (a)–(c) —
  scalar early-return, lazy visited map, trust-definite-scalar guard —
  remained valid under COW and could land first.)
- **The anchor exemptions were the open aliasing bugs.** To avoid
  pathological re-cloning, `mir_var_rhs_keeps_mutable_alias` exempted
  `pn`-call results, bare mutable identifiers, and projections rooted in
  them — those sites aliased (the C4.1 catalog: `let g`/`var h` probe,
  cd.ls). The line was "anchor sites copy, exempt sites alias." Eager
  cloning could not afford to move that line; **lazy COW removed the
  exemptions' reason to exist** — binding a `pn` result became O(1)
  share-and-mark — so full C4 semantics became affordable uniformly. That
  was the semantic payoff, on top of the perf one.
- **Allocation-bound rows showed the ceiling**: gcbench/splay/binarytrees
  were dominated by container allocation+GC; a copy discipline that
  multiplies allocations was priced directly into them. `awfy/cd` (excluded
  as wrong_output) was the correctness face of the same coin: it relied on
  aliasing that C4 forbids.

Scope note: the R0b LambdaJS regressions (sieve/puzzle/array1 — lost native
specialization) were not copy-related and were out of scope.

### C.3 Layer-2 options deliberation (decided by CW3/CW4)

| Signal | Per-object cost | Mutator traffic | Precision | Reclamation | Notes |
|---|---|---|---|---|---|
| **(a) Full refcount** | a count word (header growth) | inc/dec on every share/drop | exact; reverts to unique when count falls to 1 | RC itself can reclaim; no cycle collector needed under C4 | The classic answer. Non-atomic here (isolates). Biggest cost is the *decrement discipline*: every scope exit, container overwrite, and frame teardown must dec — a pervasive, bug-prone protocol the tracing GC currently spares us |
| **(b) Small saturating count (2–4 bits)** | spare header bits | inc on share; dec optional (can skip — saturate instead) | exact below the cap; degrades to "shared forever" past it | GC reclaims (count is advisory only) | The midpoint: exact uniqueness for the dominant low-sharing case, near-1-bit cost. No decrement protocol needed if paired with GC refresh (d) |
| **(c) 1-bit shared flag** | bit 0 of the prepared `cow_state` byte (no header growth) | one OR on share; **no decrements, ever** | over-approximates: monotonic — once shared, never reverts (until (d)) | GC reclaims | Simplest possible: cannot leak, cannot underflow, no protocol. Cost: copies that full RC would have avoided after sharing ends |
| **(d) GC-refreshed signal** (add-on to b/c) | none beyond b/c | none | restores reversion: the tracing GC — which already visits every live object — recomputes "single referrer?" during marking and resets the bit/count | unchanged | Recovers most of full RC's precision without any mutator decrement traffic; marking must distinguish in-degree 1 vs ≥2 instead of a plain mark bit |
| (e) No signal — always copy on mutation | — | — | — | — | **Rejected**: O(size) per mutation; explicitly ruled out by §9.5.1 |
| (f) Pure persistent structures, no in-place | — | — | — | — | **Rejected**: pays spine-copy even when unique; fails obligation 1 (the push/splice hot path) |

Two asymmetries framed the choice:

- **A false "shared" is only a wasted copy; a false "unique" is a semantic
  bug** (visible mutation through an alias — the C4.1 class). Every default
  must err toward shared, and simplicity of the setting protocol is itself a
  correctness feature. This favored (c).
- Lambda **already runs a tracing GC it cannot retire** (LambdaJS objects
  form cycles even though Lambda values cannot). Reclamation was therefore
  already paid for — the signal only needed to answer *uniqueness*, not
  *liveness*. This removed full RC's main structural advantage and favored
  (b)/(c)/(d) over (a).

### C.4 The clone context: retirement rationale (executed by Stage 1)

Before Stage 1: `fn_mutable_value` + `MutableCloneContext` (identity hashmap
for DAG preservation) eagerly deep-cloned at anchor sites (C.2). **CW10**
replaced the anchor with share-and-mark for Stage-1 kinds:

- `fn_mutable_value`'s generic-container deep-clone body and
  `MutableCloneContext` were deleted from the binding/assignment path.
  ArrayNum retained a narrow representation-specific compatibility path
  (its eager clone in `cow_bind_var`; retires under CW32). collatz-class
  anchor traffic dropped to a bit-OR.
- The **anchor exemptions were removed** (`pn`-result, mutable identifier,
  projection — `mir_var_rhs_keeps_mutable_alias`), closing the C4.1 aliasing
  bugs uniformly; goldens that pinned the hybrid were updated deliberately.
- **What legitimately remains of deep copy:** true ownership-transfer
  boundaries that leave the value heap — isolate messages (K13
  copy-as-value), exports that materialize outside Item space. That utility
  keeps a visited map (eager deep copy of a DAG still needs dedup) but is a
  rarely-used boundary function, not the mutation mechanism. Post-C4 it
  never sees cycles.
- **DAG note:** the eager clone needed its visited hashmap to keep shared
  substructure shared (else exponential blowup on deep DAGs); lazy COW never
  deep-copies, so the problem vanished rather than being solved.

### C.5 Stage-1 phase plan (P0–P6, executed)

| Phase | Content | Gate |
|---|---|---|
| P0 | CW5 counters on the *current* anchor (`fn_mutable_value` call rate, clone bytes, per-type) + fixtures: C4.1 probe goldens, editor/document benchmark (C4.3 gate), gcbench/splay/collatz A/B harness | counters visible in release |
| P0b | Freeze CW21's raw-versus-`_cow` API boundary, mutation-owner/writeback ABI, precise-rooting rules, shared-shape invariant, and `VMapVtable` snapshot contract before changing semantics | API call-site inventory; focused nested-write, forced-GC, VMap backend fixtures |
| P1 | `COW_STATE_SHARED` helpers + one-level copy for the five Stage-1 kinds; `MarkBuilder` static marking (CW8), all behind `LAMBDA_COW=1`; no anchor retirement yet | baseline 100% flag-off and flag-on; current observable semantics unchanged; ASan/forced-GC clean |
| P2 | Implement thin `_cow` wrappers over unchanged raw mutators; refactor every Lambda mutation choke point to the wrappers and prove nested owner writeback; ordinary VMap snapshot, immutable-task rejection, host-backend capability rule | Lambda + Radiant baselines; JS raw-mutator alias fixtures; focused map/object/element/VMap and nested-spine fixtures |
| P3 | JIT inline unique test/cold-copy branch; Layer-1 scalar/fresh-value elision (CW2); direct MIR stores regain the guarded fast path | MIR budgets; release A/B shows no unexplained regression on unique mutation |
| P4 | Only after P3 passes: CW10 anchor → share-and-mark for Stage-1 kinds; remove applicable exemptions; retain a narrow ArrayNum compatibility path; split genuine boundary deep copy | C4 core-container probe goldens flip; Lambda + Radiant baselines; test262/Node as shared-runtime regression gates only |
| P5 | MarkEditor/Radiant retained-`Item` audit and production benchmark/counter record | Radiant baseline 100%, editor/document benchmark, release Result11 record |
| P6 | Evaluate (d) GC-refresh and (b) saturating count against CW5 production-corpus data | **still open by design** — designer decision per CW4 |

**Stage-1 performance ruling (executed):** no arbitrary uniform percentage
cutoff; Result11 measured with the Result9/10 clean-release three-run-median
output-checked protocol, required to be materially better than Result10
overall and target Result9-class or better, with unique-mutation,
scalar-heavy, allocation-heavy, and editor/document rows inspected
separately. Missing or wrong-output rows do not improve the score.

---

*Cross-refs:* C4 semantics `doc/Lambda_Formal_Semantics.md` §9 (+§9.6 math
note); decision records `vibe/Lambda_Semantics_Formal.md` C4–C4.4; nested
mutation `vibe/Lambda_Design_Nested_Mutation.md` (CW22–CW28); anchor
diagnosis `vibe/impl/Lambda_Impl_Tune3.md` §4 (M3); model survey
`vibe/Lambda_Design_Memory_Model.md`; benchmarks
`test/benchmark/Overall_Result9.md` and
`test/benchmark/Overall_Result10.md`; implementation
`vibe/impl/Lambda_Impl_Tune_COW (done).md` (Stage 1) with §0 ledger
mirroring Appendix B.

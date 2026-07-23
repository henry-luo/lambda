# Lambda Design: Copy-on-Write for Mutable Value Semantics

- **Status:** PROPOSED — rev 5, 2026-07-23. **Layer-2 v1 mechanism decided by
  designer: the 1-bit shared flag (CW3), simplicity first.** The
  saturating-count upgrade is explicitly deferred behind future deep
  benchmarking on live/production Lambda scripts (CW4). **Rev 2 adds §9
  (CW15–CW18): the dual-language ArrayNum/mutable-view policy, per designer
  ruling 2026-07-23** — one storage layer, Lambda follows C4, JS follows JS
  semantics, maximal shared implementation. **Rev 3 (CW19, designer
  2026-07-23): two-stage delivery — Stage 1 (§10) is basic COW with NO
  exclusivity enforcement, performance first; Stage 2 (§11) records the full
  exclusivity/borrow-discipline design now, implemented later.** **Rev 4
  narrows Stage 1 to Lambda-native `Array`, `Map`, `Object`, `Element`, and
  `VMap`; ArrayNum COW and JS↔Lambda interop are Stage 2 because both require
  representation-specific, performance-sensitive designs.** **Rev 5 accepts
  the remaining Stage-1 decisions: VMap uses a mandatory backend
  snapshot/detach hook; nested mutation uses replacement-returning owner
  writeback; Map/Object shapes are shared only under detach-before-mutate;
  and copied spines use precise rooting. Performance has no arbitrary
  per-benchmark percentage cutoff: Result11 must be materially better than
  Result10 overall and target Result9-class or better Lambda/MIR performance.**
  Everything else is proposed for review.
- **Owns:** the engineering realization of C4's copy discipline — uniqueness
  tracking, the copy path, retirement of the eager clone anchor, and the
  related value-sharing/interning rules.
- **Semantic authority:** `doc/Lambda_Formal_Semantics.md` §9 (C4 — mutable
  value semantics). This document may not change what a program observes
  (P6: sharing must be unobservable); every mechanism here is implementation
  freedom under that contract.
- **Supersedes / absorbs:** `vibe/Lambda_Impl_Tune.md` §4 (M3 "the COW
  anchor" — ON HOLD precisely for this design; its site inventory and
  remedies are absorbed below). Companion surveys:
  `vibe/Lambda_Design_Memory_Model.md` §2.4 (Perceus), §5–6 (composition).
- **Convention:** `file:line` refs drift; confirm against symbol names.

---

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
   §9.5.2. Stage 1 must define how each copied child is written back into its
   copied parent; a local `c = cow_copy_level(c)` alone is insufficient.
   New surface ergonomics remain separate, but correct O(depth) spine
   propagation for existing nested assignment is a Stage-1 prerequisite.

Current implementation state is the C4.1 bug catalog: uniform reference
aliasing wherever the eager anchor (§2) doesn't fire. COW is therefore not an
optimization of a working model — **it is the C4 migration mechanism**.

## 2. Context B — Result10: the copying cost is already measured

`test/benchmark/Overall_Result10.md` (2026-07-22, revised in place after the
R0a fix): MIR dedup geo **4.26× (R9) → 8.42× vs Node** (1.75× worse
like-for-like, 52 common rows); LambdaJS **13.1× → 16.7×** like-for-like.
Three findings are directly this document's business:

- **The eager clone anchor is a measured wall.** The transpiler wraps
  container-possible RHS of `var` decls (`transpile-mir.cpp:5506–5520`) and
  assignments (`:9786–9794`) in `fn_mutable_value()` (`lambda-eval.cpp:6076`)
  — an **eager deep clone** with a per-call visited-hashmap
  (`MutableCloneContext`, `:5806`). collatz executes it ~40–50 M times *on
  scalars* (hashmap create/destroy per int): part of the 7.47 s wall.
  `Lambda_Impl_Tune.md` M3 diagnosed it and was put ON HOLD — "designer may
  go straight to refcount COW instead of patching the eager-clone anchor."
  This design is that path. (M3's remedies (a)–(c) — scalar early-return,
  lazy visited map, trust-definite-scalar guard — remain valid *under* COW
  and can land first; a scalar skip is a scalar skip.)
- **The anchor exemptions are the open aliasing bugs.** To avoid pathological
  re-cloning, `mir_var_rhs_keeps_mutable_alias` (`:400`) exempts `pn`-call
  results, bare mutable identifiers, and projections rooted in them — those
  sites **alias today** (the C4.1 catalog: `let g`/`var h` probe, cd.ls).
  The current line is "anchor sites copy, exempt sites alias." Eager cloning
  cannot afford to move that line; **lazy COW removes the exemptions' reason
  to exist** — binding a `pn` result becomes O(1) share-and-mark — so full
  C4 semantics becomes affordable uniformly. That is the semantic payoff, on
  top of the perf one.
- **Allocation-bound rows show the ceiling**: gcbench/splay/binarytrees-class
  benchmarks are dominated by container allocation+GC; a copy discipline
  that multiplies allocations (eager cloning) is priced directly into them.
  `awfy/cd` (excluded as wrong_output) is the correctness face of the same
  coin: it relies on aliasing that C4 forbids; its C4.3 restructuring only
  performs if copies are cheap.

Honest scope note: the **R0b** LambdaJS regressions (sieve/puzzle/array1 —
lost native specialization) are *not* copy-related and are out of scope here.

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
  last-use reuse — mutates in place with no runtime check. This generalizes
  M3 remedy (c) (`mir_expr_may_return_container` trusting definite types)
  from "skip the anchor" to "skip the bit test."
- **Caveat recorded:** Koka/Lean/Roc run this analysis over full static
  types; Lambda is dynamically typed and the JIT's inference is partial, so
  Layer 1 fires *less* here than in the precedents. **Layer 2 is
  load-bearing in Lambda to a degree it is not in Koka** — which is why the
  dynamic signal, not the static analysis, is this document's center.

## 5. Layer 2 — the dynamic uniqueness signal (options, and the decision)

### 5.1 The options, in full

| Signal | Per-object cost | Mutator traffic | Precision | Reclamation | Notes |
|---|---|---|---|---|---|
| **(a) Full refcount** | a count word (header growth) | inc/dec on every share/drop | exact; reverts to unique when count falls to 1 | RC itself can reclaim; no cycle collector needed under C4 | The classic answer. Non-atomic here (isolates). Biggest cost is the *decrement discipline*: every scope exit, container overwrite, and frame teardown must dec — a pervasive, bug-prone protocol the tracing GC currently spares us |
| **(b) Small saturating count (2–4 bits)** | spare header bits | inc on share; dec optional (can skip — saturate instead) | exact below the cap; degrades to "shared forever" past it | GC reclaims (count is advisory only) | The midpoint: exact uniqueness for the dominant low-sharing case, near-1-bit cost. No decrement protocol needed if paired with GC refresh (d) |
| **(c) 1-bit shared flag** | bit 0 of the prepared `cow_state` byte (no header growth; see §5.3) | one OR on share; **no decrements, ever** | over-approximates: monotonic — once shared, never reverts (until (d)) | GC reclaims | Simplest possible: cannot leak, cannot underflow, no protocol. Cost: copies that full RC would have avoided after sharing ends |
| **(d) GC-refreshed signal** (add-on to b/c) | none beyond b/c | none | restores reversion: the tracing GC — which already visits every live object — recomputes "single referrer?" during marking and resets the bit/count | unchanged | Recovers most of full RC's precision without any mutator decrement traffic; marking must distinguish in-degree 1 vs ≥2 instead of a plain mark bit |
| (e) No signal — always copy on mutation | — | — | — | — | **Rejected**: O(size) per mutation; explicitly ruled out by §9.5.1 |
| (f) Pure persistent structures, no in-place | — | — | — | — | **Rejected**: pays spine-copy even when unique; fails obligation 1 (the push/splice hot path) |

Two asymmetries frame the choice:

- **A false "shared" is only a wasted copy; a false "unique" is a semantic
  bug** (visible mutation through an alias — the C4.1 class). Every default
  must therefore err toward shared, and simplicity of the setting protocol is
  itself a correctness feature. This favors (c).
- Lambda **already runs a tracing GC it cannot retire** (LambdaJS objects
  form cycles even though Lambda values cannot). Reclamation is therefore
  already paid for — the signal only needs to answer *uniqueness*, not
  *liveness*. This removes full RC's main structural advantage and favors
  (b)/(c)/(d) over (a).

### 5.2 Decision

- **CW3 (DECIDED, designer, 2026-07-23): v1 ships the 1-bit shared flag (c).**
  Keep it simple first: no decrement protocol, no header growth, monotonic
  semantics that are trivially auditable. Accepted cost: post-sharing copies
  that (a)/(b)/(d) would avoid.
- **CW4 (DECIDED, designer, 2026-07-23): the switch to a small saturating
  count (b) — and the (d) GC-refresh — are deferred** pending deep
  benchmarking on a corpus of live/production Lambda scripts, not synthetic
  benchmarks alone. Prerequisite instrumentation is CW5. Both upgrades are
  bit-compatible extensions of (c): (b) widens the field, (d) adds a marking
  pass — nothing in v1 forecloses them.
- **CW5 — instrumentation ships with v1, not after.** Counters (per TypeId):
  shared-bit sets, mutations on unique (in-place), mutations on shared
  (copies taken), bytes copied. `js_exec_profile`-style, release-safe. These
  counters *are* the future benchmarking's data; without them CW4's decision
  can never be made.

### 5.3 Bit placement and the flag's exact semantics

**CW6 — bit 0 of `Container.cow_state`, not `Container.flags` and not the GC
header.** Header housekeeping has reserved byte 4 for COW without changing
the public eight-byte `Container` size or any derived-container offset:
`flags` remains byte 1, `array_flags` byte 2, `map_kind` byte 3,
`cow_state` byte 4, constructor-reserved mask bytes 5–6, and
`reserved_state` byte 7. The formerly macro-only JS-property and
constructor-reserved flags are now proper `has_js_props` /
`has_ctor_reserved` bitfields occupying bits 6/7 of `flags`; they are not
available to COW.

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

1. Binding/assignment of a container without copying — the lazy replacement
   for today's anchor (§7): set the bit on the value (and its immediate
   children are *not* touched — see Layer 3 for why that suffices).
2. Storing a container into a container (§9.3 construction capture) when the
   copy is deferred: literal build, field/index write, `push`/`splice`
   argument.
3. Returning/capturing a Lambda value or otherwise installing a second
   Lambda-visible owner.

JS↔Lambda boundary ownership is deliberately absent from this Stage-1 set
and is specified for Stage 2 in §9.3. Radiant is not treated as a special
pin class: any actual retained `Item` must go through the same owner-install
rule as other Lambda storage. The current `PersistentFieldRef` uses are raw
character references, not `Item` owners.

**Statically-owned data is born shared (CW8).** `is_static` / `is_immortal`
/ `!is_heap` containers (parser-built Mark data in the Input arena, const
pools) are never mutated in place *today* via the `is_data_migrated`
migration path; under COW the rule becomes uniform: treat them as
permanently shared — first mutation copies into the runtime heap
(materialization), exactly the existing migration pattern made lazy and
universal. `MarkBuilder` sets `COW_STATE_SHARED` at construction; no ownership
range-test is ever needed.

**Test points:** every interior-mutation entry (index/field assign,
`push`/`splice`/`pop`, `pn`-method receiver mutation, MarkEditor edits):
`owner = cow_prepare_write(owner); mutate(owner);`. The replacement must be
installed into the binding or copied parent slot before mutation. The JIT
inlines the state test + branch on hot paths (Layer 1 elides it where
uniqueness is proven); the cold copy helper returns the replacement.

## 6. Layer 3 — the copy path: one shallow level + mark children shared

**CW9 — a COW copy is one level deep: copy the container's own storage
(header + slot/field array), then set `COW_STATE_SHARED` on every *container*
child, sharing them by pointer.** Scalars copy by value (or are immutable
pointers — §8). Cost O(width of one level), never O(subtree).

This single rule *is* §9.5.1's structural sharing, without inventing a new
node representation: the existing containers become the persistent structure.

- **Spine copying requires explicit writeback.** Mutating
  `t.nodes[i].value` on a
  fully-shared `t` copies `t` (marking `nodes` shared), then `nodes` (marking
  its elements shared), then `nodes[i]` — the spine, O(depth) levels of
  O(width) — and installs each new child into the new parent. Every untouched
  subtree remains shared. Later mutations on the now-unique spine are
  in-place. The owner chain is lowered as MIR registers/stack state; Stage 1
  must not allocate a heap path descriptor on the hot path.
- **DAG shape is preserved for free.** The eager clone needed its visited
  hashmap to keep shared substructure shared (else exponential blowup on
  deep DAGs). Lazy COW never deep-copies, so shared substructure simply
  *stays* shared; divergence happens per-written-path only. (Observably
  identical either way — values have no identity; the difference was only
  ever cost.)
- **Stage-1 kinds:** `Array`, `Map`, `Object`, `Element`, and `VMap`.
  ArrayNum remains on its current specialized behavior until Stage 2; no
  generic COW test, Item walk, or visited map is added to its native path.
- **VMap is a backend contract, not a header copy (DECIDED, designer,
  2026-07-23).** `VMapVtable` gains a one-level snapshot/detach operation.
  The hook is called only on the shared cold path; unique VMap mutation keeps
  the existing direct vtable `set` after the common state test. The ordinary
  HashMap backend clones its table and insertion-order storage in one pass,
  preserving capacity/hash metadata where safe rather than rebuilding by
  repeated insertion, and marks container keys and values shared. Task
  handles stay immutable. A host-backed VMap can participate only when its
  visible backing is snapshot-stable and the hook produces independent
  writable value storage. A missing hook means immutable and mutation is
  rejected. Generic Lambda map mutation must not fall through to an external
  host setter; capability side effects use an explicit host method/API rather
  than leaking reference semantics into a Lambda value. Counters/fixtures
  require zero snapshots for unique HashMap mutation, exactly one snapshot at
  first shared mutation, preservation of the old value, and no external
  setter call on a rejected capability mutation.
- **Map/Object shape metadata is shared, not copied per value.** `TypeMap` /
  `ShapeEntry` must be immutable once shared by COW values. Existing
  type-changing field writes must take their existing shape-transition or
  detach path before changing metadata; an audit of the current in-place
  `ShapeEntry::type` writes is a Stage-1 gate.
- **Undo/history falls out** (§9.5.1): a retained old root is a full snapshot
  at O(spine) cost — the editor gets persistence as a side effect.
- **§9.5.2 interplay:** explicit owner writeback makes the *naive*
  get-modify-put spelling safe (never O(size), worst case O(spine)); the
  *guaranteed* in-place
  designs there (path-shaped `var` borrows, `_modify`-style accessors)
  remain the ergonomics work and compose with — do not replace — this.

## 7. The clone context: retired from the mutation path

Today: `fn_mutable_value` + `MutableCloneContext` (identity hashmap for DAG
preservation) eagerly deep-clone at anchor sites (§2).

**CW10 — for Stage-1 kinds, the anchor becomes: set `COW_STATE_SHARED` on the
RHS value and bind the pointer. No clone, no context, no hashmap — O(1).**
First
subsequent mutation (of either holder) pays one Layer-3 level. Consequences:

- `fn_mutable_value`'s generic-container deep-clone body and
  `MutableCloneContext` are deleted from the binding/assignment path.
  ArrayNum retains a narrow representation-specific compatibility path until
  Stage 2; Stage 1 must not route it through the generic COW implementation.
  collatz-class anchor traffic (a scalar or a bind) drops to a bit-OR.
- The **anchor exemptions can then be removed** (`pn`-result, mutable
  identifier, projection — `mir_var_rhs_keeps_mutable_alias`), closing the
  C4.1 aliasing bugs uniformly: the exemptions existed only because eager
  re-cloning was pathological, and O(1) share-and-mark is not. This is the
  step that moves the "anchor sites copy, exempt sites alias" line to plain
  C4 — goldens that pinned the hybrid (M3 §4.4 fixtures) get updated here,
  deliberately.
- **What legitimately remains of deep copy:** true ownership-transfer
  boundaries that leave the value heap — isolate messages (K13
  copy-as-value; may itself become share+mark if sender provably drops), and
  exports that materialize outside Item space. That utility keeps a visited
  map (eager deep copy of a DAG still needs dedup) but becomes a rarely-used
  boundary function, not the mutation mechanism. Post-C4 it never sees
  cycles; during migration (pre-C4.1-fix data can alias) the visited map
  also serves as the cycle guard — keep it until the C4.1 fixtures are green
  under the new anchor, then simplify.

## 8. Co-design: value sharing and interning (strings first)

The complement of COW: **immutable values never need it.** COW machinery
applies only to the Stage-1 mutable kinds listed in §6; everything else shares
unconditionally because sharing an immutable value is unobservable:

- **CW11 — strings, symbols, binary, decimal, datetime share by pointer on
  every copy, no bit, no clone.** Already true mechanically
  (`clone_mutable_item`'s `default: return value`); under C4 it is *correct
  by immutability*, now stated as a rule: `String` (`lambda.h:613` —
  `{len, is_ascii, chars[]}`) has no mutation API and must never grow one;
  string "mutation" (`fn_strcat` etc.) always builds a new value.
- **CW12 — interning status quo is kept; no global hash-consing.** Names and
  symbols ≤32 chars intern in the NamePool (pointer-equality fast path per
  A6); map keys/field names intern via the shape/name machinery. A global
  runtime string table (hash-cons every `fn_strcat` result) is **rejected
  for now**: it taxes every creation to speed repeated equality, and A6's
  pointer-first compare already harvests the interned-name win. Revisit only
  on CW5-counter evidence.
- **CW13 — targeted caches, evidence-gated:** empty-string singleton,
  single-char string table (JS-engine precedent), small-int→string cache.
  Each is a few lines and measurable; none lands without a counter showing
  the allocation actually recurs.
- **CW14 — const-literal hoisting composes with CW8:** an all-constant
  container literal can be built once (arena/const pool, born shared)
  and share-and-marked per evaluation instead of rebuilt — the same
  mechanism as static Mark data. Candidate follow-up once v1 lands.

## 9. Stage-2 design: ArrayNum, mutable views, and JS↔Lambda interop

Designer ruling (2026-07-23): ArrayNum views serve **two languages**. Under
Lambda they follow C4 — *"ArrayNum should behave exactly the same as Array
from the user's perspective."* Under JS they follow JS semantics (TypedArray
views alias their ArrayBuffer by spec). Design and implementation cater for
both and share as much as possible. **CW15–CW18 are design requirements, not
Stage-1 implementation scope.** ArrayNum is performance-critical native
storage; a naive generic COW treatment would defeat its purpose.

### 9.1 CW15 — the layering (DECIDED, designer, 2026-07-23)

**One shared storage layer; two thin per-language policy layers; no language
semantics inside the storage layer.** This formalizes what the tree already
half-does: `JsTypedArray` *already rides ArrayNum* — `ta->view` is an
`ArrayNum` and its shape side-table an `ArrayNumShape`
(`js_typed_array.cpp:384–439`), with `JsArrayBuffer` layered on top for
buffer identity/detach/resize.

| Layer | Contents | Owner |
|---|---|---|
| **Storage (shared)** | elem-kind enum + load/store dispatch, `ArrayNumShape` stride/shape math, slicing, view descriptor (`is_view`/`is_mutable_view`, base + `extra` shape), SIMD kernels, memcpy copy, bounds checks, growth | one implementation, language-free |
| **Lambda policy** | the shared bit in `cow_state` + COW test at mutation entries (§5.3); view/borrow rules of CW16 | Lambda runtime + MIR transpiler |
| **JS policy** | `JsArrayBuffer` identity, multi-wrapper aliasing, detach, resize/length-tracking, species — per ECMA spec | `js_typed_array.cpp` |

### 9.2 CW16 — Lambda policy: representation invariance + borrow-only write-through

1. **Representation invariance.** ArrayNum is an optimization of
   representation, not a semantic type: it participates in COW identically to
   Array (shared-state test; copy = one `memcpy`, no children to mark) and must
   be observably indistinguishable from Array — equality, `type()`/`is`,
   iteration, mutation semantics. **Consequence:** the known ArrayNum `==`
   representation-sensitivity (Typed-Array-4 note: value-equal arrays
   comparing unequal across representations) is *by this ruling a bug*, folded
   into OI-1; it must be fixed with or before this design ships.
2. **Read views stay first-class values — COW makes them correct for free.**
   A read view (`is_view && !is_mutable_view`; write-rejected today at the
   guards, `lambda-data-runtime.cpp:574` et al.) is a zero-copy slice: create
   it, mark base and view shared. Snapshot semantics then falls out of
   Layer 2: whichever holder mutates first COW-copies its storage and the
   other keeps the old bytes — observably a copy at creation (P6), costing
   nothing until divergence.
3. **Write-through (mutable) views are borrows, never values.** Under C4 a
   first-class write-through view is a reference cell — banned. The construct
   survives as the **array form of the path-shaped `var` borrow**
   (`doc/Lambda_Formal_Semantics.md` §9.5.2): legal only in `var`-param /
   receiver position, exclusivity-checked, non-escaping (not bindable,
   returnable, or storable). Same `is_mutable_view` machinery underneath;
   confinement is static.
4. **Borrow requires unique storage (un-share at borrow).** Creating a
   mutable borrow over shared storage first COW-copies the base at
   the borrow root — otherwise write-through would mutate bytes value-holders
   snapshotted. Swift's inout-uniqueness is the precedent. After the one-time
   un-share, the borrow writes raw (CW1: no per-write cost).
5. **Migration.** Today's mutable views are first-class bindables
   (`lambda-vector.cpp:3082`/`:3180`, leading-axis row views
   `lambda-data-runtime.cpp:647` — Scope-3 image toolkit, procedural
   in-place updates). Most call sites already use them in `pn` in/out roles,
   so the change is largely signature formalization; `proc_view_mutable` and
   the image-toolkit suite are the fixtures.

### 9.3 CW17 — JS policy: reference semantics preserved; detach at the boundary

Within JS, TypedArray/ArrayBuffer aliasing is untouched: many wrappers over
one buffer, `subarray`, `DataView`, resize/detach all behave per spec, and
**JS-owned buffers never consult Lambda `cow_state`** — JS hot stores stay
branch-free. The two languages meet only at the boundary:

- **Ingress (JS → Lambda):** a shared mark alone is insufficient if the
  Lambda wrapper still points at bytes JS can mutate. Ordinary
  `ArrayBuffer`/TypedArray ingress must either transfer/detach ownership or
  copy into Lambda-owned storage. `SharedArrayBuffer` ingress always copies.
  A future explicit read-only foreign-buffer contract may share storage, but
  cannot be inferred from ordinary JS values.
- **Egress (Lambda → JS), writable view requested:** crossing is a share
  event. **Detach-at-wrap:** if the ArrayNum is shared, copy once into a
  JS-owned buffer at wrapper creation. Lambda holders keep their snapshot;
  every JS alias derives from the one buffer made at that crossing, so JS
  aliasing among themselves is intact; and the copy is boundary-grained (one
  memcpy per crossing), never store-grained. Two separate crossings of the
  same Lambda value yield two buffers with no cross-aliasing — which is
  exactly value-handoff semantics.
- Read-only egress (formatter/inspection paths) may share storage only under
  an explicit non-retaining/non-writing contract; a future read-only-wrapper
  optimization is possible but is not implicit in ordinary JS objects.

### 9.4 CW18 — the sharing discipline (what must not fork)

Everything in the storage row of §9.1 exists **once**. The only per-language
code is the Lambda bit-test/borrow rules and the JS buffer-identity layer.
Standing guard (CLAUDE rule 13): any near-duplicate arising between
`js_typed_array.cpp` and `lambda-vector.cpp`/`lambda-data-runtime.cpp` gets
hoisted into the storage layer, never copied.

## 10. Implementation plan — Stage 1: efficient Lambda core-container COW

**CW19 (DECIDED, designer, 2026-07-23): two-stage delivery.** Stage 1 is this
plan — get Lambda-native COW and its performance right for `Array`, `Map`,
`Object`, `Element`, and `VMap`, with **no exclusivity enforcement**: no
call-site overlap checks, no borrow confinement, no global-borrow rule.
ArrayNum COW/views and JS↔Lambda interop are Stage 2 (§9 and §11.8).
Stage 1 is therefore the **C4 core-container subset**, not a declaration
that every representation and cross-language boundary has completed C4.

Overlapping writers behave as they do today (unchecked aliasing through the
borrow channel); the C4.1 fixtures keep pinning that line and Stage 1 must
not move it. For in-scope Lambda containers, Stage 1 protects value-holders:
un-share-at-borrow and the C4.2c snapshot-ordering rule land here, so the
unchecked residue is writer-vs-writer overlap. ArrayNum retains its current
specialized path until Stage 2 rather than receiving a naive generic COW
implementation.

Stage-1 ordering obligations (mechanism correctness, not checks — fixtures
required): (a) value arguments capture their snapshot **before** any
borrow's un-share/raw-write begins (C4.2c); (b) an assignment whose RHS
borrows the target's root (`a[i] = g(var a)`) must resolve the store address
**after** RHS evaluation — un-share-at-borrow may move the storage.

| Phase | Content | Gate |
|---|---|---|
| P0 | CW5 counters on the *current* anchor (`fn_mutable_value` call rate, clone bytes, per-type) + fixtures: C4.1 probe goldens, editor/document benchmark (C4.3 gate), gcbench/splay/collatz A/B harness | counters visible in release |
| P0b | Settle the mutation-owner/writeback ABI, precise-rooting rules, shared-shape invariant, and `VMapVtable` snapshot contract before changing semantics | focused nested-write, forced-GC, VMap backend fixtures |
| P1 | `COW_STATE_SHARED` helpers + one-level copy for the five Stage-1 kinds; `MarkBuilder` static marking (CW8), all behind `LAMBDA_COW=1`; no anchor retirement yet | baseline 100% flag-off and flag-on; current observable semantics unchanged; ASan/forced-GC clean |
| P2 | Route every in-scope mutation choke point through replacement-returning prepare-write and prove nested owner writeback; ordinary VMap snapshot, immutable-task rejection, host-backend capability rule | Lambda + Radiant baselines; focused map/object/element/VMap and nested-spine fixtures |
| P3 | JIT inline unique test/cold-copy branch; Layer-1 scalar/fresh-value elision (CW2); direct MIR stores regain the guarded fast path | MIR budgets; release A/B shows no unexplained regression on unique mutation and identifies every new branch/allocation on affected paths |
| P4 | Only after P3 passes: CW10 anchor → share-and-mark for Stage-1 kinds; remove applicable exemptions; retain a narrow ArrayNum compatibility path; split genuine boundary deep copy | C4 core-container probe goldens flip; Lambda + Radiant baselines; test262/Node as shared-runtime regression gates only |
| P5 | MarkEditor/Radiant retained-`Item` audit and production benchmark/counter record | Radiant baseline 100%, editor/document benchmark, release Result11 record |
| P6 | Evaluate (d) GC-refresh and (b) saturating count against CW5 production-corpus data | designer decision per CW4 |

The final unique-mutation fast path is non-negotiable: one byte
load/test/branch, no helper call, allocation, child scan, or path-object
allocation. Known scalar RHS emits nothing; known container sharing emits an
inline OR; only dynamically typed values may call a helper. Direct MIR stores
may be temporarily routed through the audited path during bring-up, but the
guarded inline path must exist before P4 changes observable semantics.

**Stage-1 performance outcome (DECIDED, designer, 2026-07-23):** do not use
an arbitrary uniform percentage cutoff. Measure Result11 with the same clean
release, three-run median, output-correctness-checked protocol as Result9/10.
For Lambda/MIR, Result10's like-for-like overall ratio is 8.42× Node on 52
common rows; Result9 is 4.80× on that same population (4.31× in its published
all-timed deduplicated headline). Result11 must be **materially better than
Result10 overall** and target the **Result9 class or better**. Unique
container mutation, scalar-heavy rows, allocation-heavy rows, and the
editor/document workload are inspected separately so a headline geometric
mean cannot hide a COW hot-path regression. Missing or wrong-output rows do
not improve the score.

All allocating copy paths use precise `RootFrame` / `Rooted` ownership for
the source, replacement, incoming value, and owner chain, and reload moved
pointers after a possible GC. Forced-GC stress (`LAMBDA_GC_FORCE_*`) is a
gate throughout; the `let g`/`var h` probe is the false-unique canary.

## 11. Stage 2 — exclusivity & borrow discipline (design recorded, deferred)

**CW20 — the complete check design.** Recorded now so Stage 1 can proceed
without it and Stage 2 can start from a settled shape. Nothing here ships in
Stage 1.

### 11.1 Why the check exists

The `var` borrow is the one deliberate hole C4 punches in "values never
alias": for the duration of a call, two names refer to one storage.
Exclusivity keeps that hole single-writer. It is the precondition for four
things: the callee's local reasoning (its params are independent values);
**CW1's zero-cost raw borrows** (no shared-state consult — sound only if no
other observer exists during the borrow); the JIT's no-alias optimization
freedom (every `var` param is `restrict` by construction — the Fortran
argument-rule advantage); and the §9.6 formal reading (each `var` an
independent `x′ = f(x)` sequence). With invariance it also closes the
covariant-store hole (§9.2). Without the check, `f(x, x)` silently
reintroduces aliasing *at the call site, invisible in the callee* — and the
double-borrow case has no good outcome under un-share-at-borrow: not copying
aliases the writers; copying detaches the second borrow into a dropped
duplicate (silent lost updates). Hence: reject at compile time.

### 11.2 The structural fact that bounds the design

Borrows live only for the duration of a call, and the runtime is
single-threaded — so while a borrow is alive, the only code running is the
callee and its callees. A second live writer can therefore arise from exactly
**two** sources: (i) another borrow born at the same call site; (ii) a name
the callee reaches independently — module-level / view-state `var`s.
Everything else is sequential with the borrow by construction. This is what
keeps the check local and cheap — no lifetime inference, no whole-program
analysis (`Lambda_Design_Memory_Model.md` §2.3's promise).

### 11.3 The call-site check — one overlap predicate, four faces

Enumerate the call's `var` arguments (the `pn`-method receiver counts as
one), test pairwise overlap:

| Face | Overlap to reject | Example |
|---|---|---|
| two+ `var` args | same variable | `f(x, x)` |
| receiver vs `var` args | receiver is a `var` param | `list.append_all(list)` |
| path borrows (with `doc/Lambda_Formal_Semantics.md` §9.5.2) | **path-prefix** relation: `x` vs `x.f` conflict; `x.a` vs `x.b` fine | `f(var t, var t.nodes[i])` |
| mutable view borrows (§9/CW16) | **region** overlap | `f(var view(img,r1), var view(img,r2))` |

**Granularity ladder** (start coarse, refine on demand): Stage-2 v1 =
whole-base conservative (same base ⇒ conflict; sound, rejects some safe
programs). Upgrades, cheapest first: (a) **disjointness-by-construction
splitters** — a blessed `split(var arr, at)` / `rows(var img)` /
`tiles(var img, n)` builtin takes *one* borrow of the base and returns
provably-disjoint mutable views in a single operation (Rust `split_at_mut`
precedent); likely sufficient for the image toolkit with no new analysis;
(b) static range-disjointness for constant indices; (c) Swift-style dynamic
bookkeeping — record `(base, byte-range)` per active borrow, check overlap
at borrow creation (cost per borrow, not per write) — for dynamic ranges.

### 11.4 The one non-local case: module-level and view-state `var`s

```lambda
var g_list = [...]
pn push_sorted(var list: int[]) { ... g_list ... }  // callee also names the global
push_sorted(g_list)                                  // two write paths, one storage
```

No call-site inspection can see this — it depends on what the callee
transitively touches. Options: **(a) forbid passing module-level /
view-state `var`s as `var` arguments** — compile error with a teaching
message ("name the module var directly in the `pn`, or copy in/out");
recommended Stage-2 starting rule, matching the C4.2a start-strict
philosophy; **(b)** a dynamic borrowed-bit on globals that are ever borrowed
(Swift's Law-of-Exclusivity dynamic arm — the precedent that this case
genuinely needs runtime help); **(c)** callee effect summaries — rejected,
whole-program analysis against the local-check spirit.

### 11.5 What needs no check — sequential by construction (do not re-add guards)

- **Borrow in argument position** — `f(var a, g(var a))`: `g`'s borrow
  completes during argument evaluation, before `f`'s borrow activates.
- **Re-borrow / recursion** — `pn f(var a) { g(var a) }`, recursive `f`: a
  single writer chain; one live writer at any instant.
- **Readers** — writer-vs-writer only (C4.2c): plain params snapshot by
  value; no read bookkeeping, ever.
- **Nested-`pn` up-level access (C4.2a relaxation)** — the enclosing body is
  suspended while a callee holds its borrow, so the nested `pn` cannot run
  concurrently with it; the global route is already §11.4's case.

### 11.6 Snapshot iteration (ruling candidate, record as C4.2d when implemented)

`for (x in arr) { push(arr, …) }` — iteration over a `var` container mutated
in the body. Value semantics' coherent answer: **snapshot iteration** —
share-mark `arr` at loop head; the first in-body mutation COW-copies; the
loop walks the entry-time value. No check, no iterator-invalidation UB —
unlike Python/JS live iteration. Same rule for pipes over `var` containers.

### 11.7 Stage-2 acceptance

The three C4.4 compile checks (`var`-args-only; exclusivity per §11.3
including receiver; `var` receiver for `pn` methods) + CW16.3 view-borrow
confinement (mutable views become non-escaping, `var`-position-only) + the
§11.4 global rule + fixtures: `f(x,x)`, `x.merge(x)`, global-borrow overlap,
disjoint-tiles rejection under the conservative rule (documents the
intentional false positive), snapshot-iteration goldens.

### 11.8 ArrayNum and JS↔Lambda boundary work

Stage 2 also owns the implementation of §9:

- ArrayNum COW must preserve native packed storage, SIMD/vector kernels, view
  math, and branch-free proven-unique stores. Its design includes
  representation-invariant equality and the read-view/mutable-borrow split;
  it is not an instantiation of the generic Item-container copier.
- JS↔Lambda interop must implement the ingress/egress ownership matrix in
  §9.3. Ordinary JS mutable buffers cannot become Lambda snapshots by merely
  setting a bit; `SharedArrayBuffer` always copies; writable Lambda→JS
  crossings receive JS-owned storage; only explicit read-only contracts may
  share bytes.
- Stage-1 test262/Node runs are regression gates for shared runtime changes,
  not evidence that these Stage-2 boundary semantics have landed.

## 12. Settled Stage-1 decisions and residual risks

### 12.1 Settled Stage-1 implementation contract

These are accepted decisions, not open design questions:

1. **Mutation ownership/writeback ABI (DECIDED).** A copied container returned
   only to a local variable is lost, so every mutation lowering owns an
   assignable root/parent slot. `cow_prepare_write(Item)` returns the
   replacement; nested writes propagate and install replacements along the
   owner chain. The chain stays in MIR registers/stack state, never a heap
   descriptor. P0b fixtures must prove root and deep-spine writeback.
2. **VMap snapshot/detach contract (DECIDED).** `VMapVtable` gains the hook.
   Mutable backends implement it; immutable capability VMaps reject mutation.
   A backend whose `set` means an external side effect is not a C4 value and
   cannot silently use Lambda map mutation. Snapshot-stable backends call the
   hook only on the shared cold path; external capability effects use an
   explicit non-map-mutation API.
3. **Performance outcome (DECIDED).** There is no blanket 3% cutoff. Every
   design choice remains performance-accounted, and Result11 uses the
   Result9/10 release protocol. Lambda/MIR must be materially better than
   Result10 overall and target Result9-class or better performance; focused
   hot-path results and counters must agree with the overall result.
4. **Shape metadata invariant (DECIDED).** Map/Object values share shapes for
   efficiency. Every `ShapeEntry::type` mutation first detaches/transitions
   the shape; otherwise a value-level copy could mutate another value's
   interpretation.
5. **GC rooting of a copied spine (DECIDED).** Source, replacement, incoming
   value, and intermediate parents are precisely rooted across allocations
   and reloaded after possible compaction. Forced-GC coverage is mandatory.

### 12.2 Deferred questions and residual risks

1. **Stage-2 granularity endpoint** (design ladder recorded in §11.3): does
   the image toolkit need more than splitters — i.e., do dynamic
   `(base, range)` checks ever pay their way? Decide on real toolkit code
   when Stage 2 starts.
2. **Migration hazards:** stale pre-C4 comments claim `push`/`splice`
   sharing semantics that the exemption removal changes; scripts relying on
   aliasing (cd.ls) need their C4.3 restructuring in the same window as P4.
3. **`Element`/document depth:** Layer-3 spine cost is O(depth×width); deep
   narrow documents are fine, shallow enormous fan-out nodes (10⁵-child
   element) pay O(width) per copy of that node — the §9.5.1 node-
   representation question (chunked children) stays open for exactly this
   case; the editor benchmark decides.

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

> Companion to `vibe/Lambda_Impl_Tune_COW.md` §0 (whose ledger marks these
> OUT). When that plan completes, **this list is what remains open of C4** —
> the checklist for declaring C4 fully done. Recorded 2026-07-23.

### B.1 Owned by Stage 2 of this document (§11)

| Item | Source | Pick-up trigger |
|---|---|---|
| **ArrayNum COW and view policy** — native packed-copy strategy, representation-invariant equality, read-view snapshots, mutable-view borrows | §9.1–§9.2; §11.8 | Stage 2; preserve the current specialized ArrayNum path throughout Stage 1 |
| **JS↔Lambda ownership boundaries** — mutable-buffer ingress/egress, `SharedArrayBuffer`, explicit read-only sharing | §9.3; §11.8 | Stage 2; Stage-1 JS suites are regression-only |
| Exclusivity checks — all four call-site faces (`var`-vs-`var` args, receiver-vs-args overlap, path-prefix, view-region) + the `var`-args-only check | C4.4 #1; §11.3 | Stage-2 start; fixtures `f(x,x)`, `x.merge(x)` stay pinned at *current* (aliasing) behavior with a stage-2 marker until then |
| Module-level / view-state `var` passed as `var` arg (the non-local overlap) | §11.4 | Stage-2 start; recommended opening rule = forbid, with teaching error |
| View-borrow **confinement** (mutable views become non-escaping, `var`-position-only) | CW16.3; §11.7 | Stage 2; until then mutable views retain current behavior |
| Snapshot iteration over a mutated `var` container | §11.6 | Stage-2; record as C4.2d when implemented |
| Exclusivity granularity endpoint (splitters vs static ranges vs dynamic checks) | §11.3 ladder; §12.1 | decide on real image-toolkit code during Stage 2 |

### B.2 Needs its own design first (no owner document yet)

| Item | Source | Notes |
|---|---|---|
| **Nested-mutation ergonomics** — path-shaped `var` borrows (`f(var t.nodes[i])`), `_modify`-style in-place accessors, guaranteed get-modify-put | C4.4 #6; `doc/Lambda_Formal_Semantics.md` §9.5.2 | Stage 1 *reduces the urgency* (the naive spelling becomes safe — worst case O(spine), never O(size)) but the guaranteed-in-place forms and their syntax are undesigned. Natural sequencing: after Stage 2 (path borrows reuse the exclusivity machinery) |
| **Element/document node representation for huge fan-out** — chunked children so a one-level copy of a 10⁵-child node isn't O(width) | §9.5.1 residue; §12.2 | gate on the editor/document benchmark from Tune-COW Phase A3; only if it fails on real documents |
| **Non-escaping nested-`pn` relaxation** (direct up-level `var` access for call-position-only nested `pn`s — the closure-style parser case) | C4.2a spec sketch | backward-compatible addition; interim idiom (object with `pn` methods) is unblocked by Tune-COW Phase B, which lowers the pressure to do this soon |

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

*Cross-refs:* C4 semantics `doc/Lambda_Formal_Semantics.md` §9 (+§9.6 math
note); decision records `vibe/Lambda_Semantics_Formal.md` C4–C4.4; anchor
diagnosis `vibe/Lambda_Impl_Tune.md` §4 (M3); model survey
`vibe/Lambda_Design_Memory_Model.md`; benchmarks
`test/benchmark/Overall_Result9.md` and
`test/benchmark/Overall_Result10.md`; tuning ledger
`vibe/Lambda_Tuning_Proposal.md` (R-items); implementation
`vibe/Lambda_Impl_Tune_COW.md` (Stage 1) with §0 ledger mirroring this
appendix.

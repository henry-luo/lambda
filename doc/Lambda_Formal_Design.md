# Lambda Formal Design — Specification

**Spec version:** 1.3.0 (2026-08-06)

**Status:** normative — the single source of truth for the design and
implementation decisions that realize the semantics in
[`Lambda_Formal_Semantics.md`](Lambda_Formal_Semantics.md). Where any other
document — including the `vibe/` design records — or the implementation
disagrees, this specification wins; the design records govern the history.
Scope: the Lambda core runtime and Jube polyglot hosting. LambdaJS and
Radiant get their own documents.

**Ruling IDs.** Same convention as the semantics spec: section-path IDs
(`D2.3.1` = first ruling of §2.3), `v2` suffix on revision, doc-level
semver. The bare `D` series is reserved for this document — the
data-processing ledger was renumbered `PD1–PD16` on 2026-08-06 to free it.

**Implementation marks.** A ruling marked `*` is not, or only partially,
implemented; Appendix A carries the footnote. Open design issues are
numbered **`DO#`** (design-open) in Appendix B. Appendix C maps sections to
the decision records.

---

## D1 Architecture Principles

- **D1.1 — One host, many bundles.** There is exactly one `lambda.exe`;
  bundles differ only by the Jube modules beside it, and host binaries are
  byte-identical across bundles. Guest languages are **dialects hosted on
  Lambda's substrate** — no 100%-compat claim; a guest is a grammar + AST
  builder + LangProfile, and what the profile doesn't cover isn't
  supported. [JA4, J5, U1]
- **D1.2 — One data model.** `Item` is the only value currency — across
  languages, across the C ABI, across modules. Native structs cross as VMap
  projections; system resources as integer rids (the fd model, not the
  pointer model). [JA6]
- **D1.3 — One core runtime.** One GC, one MIR JIT, one event loop per
  context, one set of side stacks — guests **reuse contracts, not
  implementation accidents**, and pay for reuse only below the semantic
  boundary (no guest inherits another language's truthiness, coercion, or
  object model). [Lang_Hosting P1–P10, C1–C10]
- **D1.4 — Errors are return values at every boundary.** Exceptions pend;
  no C++ exception, `longjmp`, or guest unwind ever crosses a language or
  module boundary. [JA5, J3]
- **D1.5 — Precise GC everywhere, forever.** Conservative native-stack
  scanning is retired from every build and stays retired; a guest is
  precise iff it emits through the shared rooting primitives. [CR1–CR8]
- **D1.6 — The legacy paths are frozen.** C2MIR (`transpile.cpp`, `--c2mir`)
  is permanently compatibility-only: no edits, no new features, no
  validation gates, exempt from every new protocol. [rule 14; SM14, CR7, U11]
- **D1.7 — Source is the spec level.** Scripts distribute as source; MIR is
  a private in-memory IR, never a distribution format; every compiled
  artifact is a **local derived cache keyed by build ID + content hash** —
  memoization, not a format. [JA13, MC7/P14]
- **D1.8 — Decisions are carried, never recovered.** The compiler carries
  semantic contracts and representation choices forward explicitly; it
  never reconstructs either by interpreting emitted MIR register classes,
  and no context-dependent value lives at a code-baked address. [Lane §1, MT2]
- **D1.9 — Fail closed.** Unresolved calls are `MAY_GC` with unknown
  effects; unsupported representation conversions abort in every build;
  malformed input is an error, never UB; suppressing logs must not turn
  malformed MIR into accepted MIR. [Stack_API §7.2, Lane §4.3, CP18, MT2]
- **D1.10 — Enforcement is part of the design.** Every invariant exists as
  a static assert, debug check, lint rule, ratchet, or CI gate — the tag
  budget is governed, emission size is ratcheted at 0% slack, module
  boundaries are link-checked, and **every module kind names its
  conformance gate; no module lands without its gate**. [Item_Boxing R7, MT7, SM5, JA9]

## D2 Data Representation

### D2.1 The Item model

- **D2.1.1** An `Item` is one 64-bit word, passed by value, in exactly three
  storage classes: **inline tagged immediate** (bits 63–56 = TypeId, 55–0 =
  payload), **tagged scalar-leaf pointer** (same split, low 56 bits = the
  pointer), and **raw header pointer** for containers (whole word is the
  pointer; TypeId at byte offset 0 of the object). *Scalars are type-first;
  containers are data-first. Keep the hybrid: tagged leaves, raw
  aggregates, inline immediates.* [Item_Boxing §0, §3.1]
- **D2.1.2** `get_type_id(Item)` is the **only** sanctioned semantic-type
  interface (tag ≠ 0 → tag; item ≠ 0 → `*(TypeId*)item`; else null); the
  inline-double self-tag test prepends without disturbing the container
  branch. *Semantic equality has one entry point* — a raw Item word compare
  is not semantic equality unless canonicality proves it. [Item_Boxing §3.2, R9]
- **D2.1.3** Measured ruling: String/Symbol/Binary stay **tagged leaf
  pointers** — raw header pointers regressed the geomean 11.1% (55/62
  benchmarks slower) with 7 correctness hazards. Rejected likewise: tagging
  container pointers; a generic CONTAINER tag; the universal raw-header
  model. *Representation uniformity is not a goal by itself.* [Item_Boxing §4]
- **D2.1.4** Container extensibility is **layered, not tag-based**: storage
  class → `Container.type_id` family → kind/flags → shape/nominal
  type/vtable. A new top-level TypeId only for genuinely distinct language
  semantics. *The tag budget is governed, not discovered* — 64 legal
  high-byte values under inline doubles; claiming one updates the partition
  assertions and this doc in the same change. [Item_Boxing §5, R10]
- **D2.1.5** Canonical construction: one encoding per value per build;
  container constructors never OR a TypeId; leaf constructors go through
  canonical `*2it`; a null pointer becomes the canonical null Item, never a
  zero-word "container". [Item_Boxing §6.3]
- **D2.1.6*** *A storage class is not designed until its lifecycle is* —
  allocation, reclamation, and equality stated together; new tags,
  sentinels, and packed encodings are illegal until pinned by an assertion.
  [Item_Boxing R7, R8]
- **D2.1.7** One-way doors, deliberately accepted and to be re-acknowledged
  before GC/sandboxing/port work: non-moving heap, no pointer compression,
  8-byte references, the low-address container model, and the AArch64
  TBI/MTE rule that top-byte metadata never leaks into Items. [Item_Boxing §7.3]

### D2.2 Number representation

- **D2.2.1** Common doubles are **self-tagged inline** (the double's own bit
  pattern is the Item; the tag partition reserves the discriminator space);
  subnormal/colliding doubles box via number homes. One canonical
  `ITEM_DBL_MASK` discriminator; epilogues never re-derive packed tests.
  [Double_Boxing; Stack_API inv. 23]
- **D2.2.2** The v5 `int` carrier: finite values pack into the 56-bit
  payload (unbox = shift-left-8, arithmetic-shift-right-8); merged poison
  is **not** packed — it stays the inline IEEE double, so an INT-tagged
  Item always decodes to a finite band value. The native lane is **i64**
  with sentinels at the two's-complement extremes (`NAN = INT64_MIN`,
  `-INF = INT64_MIN+1`, `+INF = INT64_MAX`) — branch-free `neg`/`abs`,
  out-of-band degradation. Lane sentinels are **private to the lane** and
  never escape into an Item, container, or guest bridge. (Supersedes the
  C16-era G0 "int's native representation is the double" ruling.)
  [Int_Type §5.1]
- **D2.2.3** Wide scalars (`int64`, `uint64`, cold doubles) have **no
  inline form at any magnitude** (no value-dependent representation
  branches) and follow the scalar-home ownership taxonomy (§D5.2);
  `DTIME` is object-backed — GC-owned when dynamic, Input-arena when
  static. [Stack_API §15.1, SF16]

### D2.3 Boxing and unboxing

- **D2.3.1** A typed function compiles to a **native entry plus a boxed
  entry** per the dual-func plan (§D8.3); untyped functions are boxed-only.
  Closures keep captures as `Item` and unbox on access; union-typed
  parameters never unbox (a representation decision, not a soundness one).
  [Box_Unbox, DF §1.3]
- **D2.3.2*** **Blind `(void*)` casts of Items to container pointers are a
  defect.** Container-typed parameters unbox through type-checking helpers
  (`it2map`, `it2list`, …) returning NULL on mismatch (the `it2s`
  contract); container returns box through `p2it()` so NULL becomes
  `ItemNull`, never the zero word. [Box_Unbox2]
- **D2.3.3** Boxing is representation only (semantics S1.6); every
  box/unbox pair round-trips exactly, including poison and the nullable
  sentinels.

### D2.4 Value representation discipline

*Never recover a decision by interpreting emitted MIR register classes.*

- **D2.4.1*** Four facts, four authorities: semantic contract → AST
  `Type*`; planned representation → lowering analysis; emitted
  representation + provenance → `MirValue`; physical register class →
  `MirValue.mir_type`. Under v5, boxed Item / int lane / Lambda `int64` /
  machine quantity all share `MIR_T_I64`, so `MIR_reg_type()` probing is
  banned in expression lowering (named physical-layer helpers only).
  [Lane §1–§4]
- **D2.4.2*** `transpile_expr()` returns a `MirValue` carrying the full
  `Type*` contract; ValueRep distinguishes `INT_LANE`, `MACHINE_I64/U64`
  from Lambda `I64`/`U64`/`F64`/`ITEM`/pointer reps;
  `lambda_canonical_rep(Type*)` never returns a machine rep for a Lambda
  type. [Lane §4.2]
- **D2.4.3*** **Representation conversion ≠ semantic coercion**:
  `em_require_rep()` changes only the carrier, keyed by (source rep,
  target rep, contract); unsupported pairs fail closed. Machine
  `INT64_MAX` is a finite quantity; the same bits in the int lane are
  `INT_LANE_INF` — no identity conversion crosses that boundary. [Lane §4.3]

### D2.5 The nullable native lane

- **D2.5.1*** `N(T?) = N(T) | NULL_LANE(T)`: optionals keep a native
  carrier everywhere a representation is chosen. Invariants:
  `box(NULL_LANE(T)) == ItemNull`; **no sentinel laundering** (test before
  arithmetic/compare/deref/store); plain-`T` carriers never hold a null
  sentinel; representation follows the **full type contract**, never
  TypeId alone; **GC sees only pointers** — numeric sentinels are never
  roots. [Nullable §1–2]
- **D2.5.2** Sentinels: `INT_LANE_NULL` (numerically the `ItemNull` word —
  what makes box/unbox exact) joins the three poison sentinels; `bool?` is
  one byte 0/1/2 with C-truthiness banned; sized `i8?`…`u32?` widen to the
  i64 lane; `i64?`/`u64?` are the deliberate exception (every bit pattern
  valid → ordinary Item lane); `float?` reserves a distinct NaN payload
  (`ItemNull`'s bits are a valid finite double) — the null test is a bit
  compare, never IEEE `==`, and other NaNs canonicalize on store.
  [Nullable §3–4]
- **D2.5.3** `a[i]` with an unproven index infers `T?` — not `T`, not
  `any`; flow-sensitive proofs may use the payload directly but never
  change the public inferred type. `any`, `number`, `integer`, and
  heterogeneous unions stay boxed. [Nullable §5]

### D2.6 Containers and array storage

- **D2.6.1** Three physical array forms: boxed Array (Items), native Array
  (uniform 64-bit lane word per element), ArrayNum (specialized numeric
  layout). Map/shape packing uses minimum field width; array packing the
  uniform word — a deliberate granularity difference. Shapes carry an
  immutable `LaneStorageDesc` from **one shared descriptor resolver** for
  MIR, map layout, arrays, and guests. [Nullable §6]
- **D2.6.2** **ArrayNum is strictly non-null**: an admitted null store
  performs a one-way, atomic demotion to native `Array<T?>`; `int[]`
  rejects the store outright. Covariant `int[] → int?[]` assignment may
  share the buffer under COW; `pn var` parameters are invariant. ArrayNum
  must be observably indistinguishable from Array (equality, `type()`,
  iteration) — representation invariance is a ruling, and the known
  representation-sensitive `==` is a bug.* [Nullable §6.3–6.4, CW16.1]
- **D2.6.3** `ELEM_INT` element storage is the i64 lane (finite values or
  poison sentinels), mapped to IEEE at print/box boundaries; both int
  element kinds share the i64 kernel path.* [Int_Type §5.8]
- **D2.6.4** Wide-scalar Items inside a container point **only into that
  container's own buffer** (tail regions, `extra` = uniform tail count);
  headers are never reallocated out from under an identity. [SF15]

### D2.7 The scalar-GC invariant

- **D2.7.1** **No standalone scalar cell in the GC heap**: no
  `INT64`/`UINT64`/`FLOAT` payload is ever allocated in the GC object
  zone; an Item with those tags can never point into it; enforced fatally
  at the shared allocation choke point in every build. *A class of
  lifetime bugs becomes unrepresentable.* (A GC object may *contain*
  scalar payload words; it just never *is* a scalar cell.) [SG1, SG4]
- **D2.7.2** Public entry wrappers use **caller-donated scalar homes** (a
  trailing home out-param; transitive forwarding — an Item pointing at a
  donated home returns only to that home's owner; NULL home aborts, *no
  silent fallback*). Rejected: callee donation (unbounded in dynamic-call
  loops), context-global slots, C-local forwarding. The ownerless-slot GC
  fallback is counted and **explicitly transitional**.* [SG2, Stack_API §15.5]

## D3 Type and Shape

Types are first-class in Lambda — composed, inferred, reflected, and
matched/queried (semantics §S10.1, §S11). This section rules the machinery
that carries them.

### D3.1 First-class type values

- **D3.1.1*** A type value is a `Type*` graph node under one compact
  TypeId (`LMD_TYPE_TYPE`), discriminated by `Type.kind`
  (simple/unary/binary/constrained) — composite and constrained types
  share the tag rather than spending tag-budget entries (D2.1.4). The
  `Type*` graph is the **semantic authority** for every contract the
  compiler carries (D2.4.1); a TypeId alone is never a contract.
  [Lane §1, lambda-data.hpp]
- **D3.1.2** Types compose as values (`|` union, `?`, `[]`, constraints)
  and compare **representationally** — normalized forms, not semantic
  equivalence (undecidable with constraints); union normalization uses a
  canonical ordering (semantics S5.5.2; ordering rules still to pin —
  DO22). [C8.5-4]
- **D3.1.3** Reflection is by operators and functions, not properties:
  `type(x)` yields the type value, `name(T)` its name; `match` arms and
  `is` dispatch through the same runtime membership operation (D3.2.1) —
  one classification machinery for the whole surface. [C9a, TE-6]

### D3.2 The subtype foundation

- **D3.2.1** **One subtype model, three distinct operations**, sharing one
  numeric foundation and never falsely identified: `subtype(S, T)` —
  static, drives inference and static admission; `matches(item, T)` —
  runtime membership, drives `is`/`match`/validator checks;
  `checked_convert(item, T)` — explicit conversion functions own lossy
  conversions. [TE-6]
- **D3.2.2*** The **validator is the runtime enforcer** for user-defined
  types: deep, on first crossing, producing a rich error with a validator
  path — or the binding never exists. Constrained types (`T where …`)
  enforce the base only, for now; `is`, `fn_is`, the emitter, and the
  validator must consolidate onto the shared foundation (a known
  three-way divergence today). [TE-10, TE-6 P5]
- **D3.2.3** Declared and effective types are **separate recorded facts**
  (declaration on the binding node, inference beside it): an annotation is
  a contract (semantics S11.4.1); inference is never a binding contract
  and may never override what it proved. [TE-1, TE §8.1]

### D3.3 Inference

- **D3.3.1** **Inference is unobservable — it buys performance only.**
  Erasing every inferred type and running boxed must produce identical
  results; this is a verifiable property (semantics §S16.3), checked by
  the boxed-vs-JIT differential harness. [B7]
- **D3.3.2** Entry-shape inference and body/result inference are
  **separate products**: float arithmetic on a parameter does not retype
  the parameter; *inference selects an implementation, it does not create
  a source contract* — and inference never creates a `^` obligation.
  [DF12, DF13, TE-13]
- **D3.3.3** Container element-type **narrowing dies with its binding**:
  a narrowed element type is a property of one binding's scope, never
  written back into the container; the double-box → `ITEM_ERROR` failure
  is the diagnostic tell for violations. [elem-type invariant]
- **D3.3.4** Representation always follows the **full inferred contract**
  (D2.4, D2.5): an unproven indexed read infers `T?`; flow-sensitive
  proofs may narrow privately but never change the public type.

### D3.4 Shapes

- **D3.4.1** A map/element shape is a `ShapeEntry` chain on
  `TypeMap`/`TypeElmt`; map data is a **packed C struct**: `byte_offset`
  precomputed as a running sum, every field 8-byte aligned, `any`/untyped
  fields are a 16-byte TypedItem. The layout is **ABI**: transpiled
  direct field access and the runtime read/store path must read the same
  bytes. [Shape_Pool §1, Transpile_Map]
- **D3.4.2** Shape identity is **structural, not nominal**: the ordered
  (field-name, TypeId) sequence, keyed by a signature of hash + length +
  byte size. Field order is significant; `byte_offset` is derivable from
  the type sequence and deliberately excluded from identity. [Shape_Pool §3]
- **D3.4.3*** Shapes intern in a **per-Input shape pool** — hierarchical
  lookup with parent inheritance (a parent hit is returned, not copied
  down), interning **at finalization, not incrementally** (builders
  construct a throwaway chain; `final()` swaps in the pooled one), and
  null-safe opt-in (no pool ⇒ per-map chains, no semantic change).
  Runtime-constructed maps do not intern today — they rebuild per
  transition. [Shape_Pool §3–§8]
- **D3.4.4** ShapeEntry name identity follows the name-identity split
  (D4.6): `name_hash` is a lookup hash, **never an identity**;
  `predefined_id` (a NameId) is the generated identity; `key_ref` is the
  canonical runtime key — and **Input-owned document shapes keep it
  NULL** (a million-key JSON must not bloat session state). [NI10, NI13]
- **D3.4.5** Shape transitions obey the **map-layout invariant**: the
  exact runtime shape always describes the stored bytes. Type-compatible
  writes stay on the direct path (same type; `int → float`;
  `int ↔ int64`); an incompatible write rebuilds the shape and repacks —
  transactionally, so on failure nothing changes (semantics S11.4.1).
  `let`-bound containers never transition (immutable); only
  `var`/procedural code can, and an annotated root must keep conforming.
  [TE §6 B7b, Transpile_Map DD3]
- **D3.4.6** Shapes carry the immutable `LaneStorageDesc` derived from the
  full `Type*` via the one shared descriptor resolver (D2.6.1); changing
  a field's *contract* re-derives layout, writing a null into a `T?`
  field does not. [Nullable §6]

## D4 Memory Management

### D4.1 Allocator tiers

- **D4.1.1** Four tiers: **GC heap** (runtime values), **Input arena**
  (parsed documents), **AST/const pool**, **NamePool** (interned names).
  The non-GC tiers are invisible to the collector: `gc_is_managed()` (a
  pointer range test) is the single gatekeeper; tracing stops at any
  non-managed pointer. [GC1 §2.10.4]
- **D4.1.2** Static Mark data is **born shared and immortal**:
  `MarkBuilder` sets the shared COW state at construction; the first
  mutation materializes into the runtime heap. Reads of wide scalars out
  of containers: **reference iff immortal storage (const pool / Input
  arena), else copy** — the discriminator is storage class, not
  mutability. [CW8, SF16]
- **D4.1.3** Input/format allocations are pool/arena-owned and **outside GC
  rooting entirely**; any future GC-heap boundary there requires an
  explicit audit. [CR8]

### D4.2 Memory Context

- **D4.2.1** `MemContext` is a **factory that owns every allocator** — no
  allocator exists off-graph; parent/backing edges wire at creation;
  cascade teardown in reverse-dependency order. It governs allocator
  *lifecycle* only — `pool_alloc`/`arena_alloc` stay lock-free and
  unchanged. [Memory_Context §2–§7]
- **D4.2.2** Per-document sub-contexts key allocators to a document URL,
  with the **attribution/reclamation split**: Radiant documents are
  reclaimable per-document; Lambda `input()` data is GC-coupled and gets
  attribution only. Stage 2 (central page allocation, OOM escalation
  ladder) is designed, not started.* [Memory_Context]

### D4.3 Garbage collection

- **D4.3.1** **Non-moving mark-and-sweep, dual-zone**: objects never move
  (Item pointers stable forever, zero write barriers); only the data zone
  compacts, with exactly one pointer fixup per surviving object — *fixup
  is O(survivors), not O(references)*. Copying GC and handle tables
  rejected. [GC2 §4]
- **D4.3.2** Object zone = size-class segregated free lists; data zone =
  bump allocator fully reset per GC; strings keep inline `chars[]` in the
  object zone. Adaptive growth threshold (×4 under 40% freed, ×2 to 74%,
  hold at ≥75%, capped). [GC2 §5, §12]
- **D4.3.3** Precise closure-env tracing via `closure_field_count`; VMap
  traces and finalizes through vtable callbacks that are allocation-free
  and never re-enter script. [GC2 §8, JA6]

### D4.4 COW implementation

- **D4.4.1** v1 ships the **1-bit shared flag** (`cow_state`, monotonic,
  idempotent; copies initialize state explicitly, never memcpy it) — *a
  false "shared" is a wasted copy; a false "unique" is a semantic bug.*
  Saturating counts / GC-refresh are bit-compatible extensions gated on
  live-corpus counters. The same bit becomes the cross-isolate
  discriminator (biased refcounting). [CW3, CW6, CW3-C]
- **D4.4.2** A COW copy is exactly **one level** — copy own storage, mark
  container children shared; O(width) per level, DAG sharing preserved
  free. Raw mutators stay policy-free; Lambda lowers through parallel
  `_cow` wrappers; JS lowering never emits `_cow`; no language-mode test
  in a raw hot path. The unique fast path is non-negotiable: one byte
  load/test/branch. [CW9, CW21, CW1/CW2]
- **D4.4.3** All copy paths precisely root source/replacement/owner chain
  and reload after possible GC; forced-GC stress is a permanent gate.
  Exclusivity checks and view-borrow confinement are Stage 2.* [CW20]

### D4.5 The Radiant seam

- **D4.5.1** One substrate (mempool/arena + Memory Context), **two
  policies**: Lambda traces; Radiant uses arenas-as-regions + type-stable
  pools + generation handles + RAII, never GC'd — *the document arena is
  the cycle collector*. Seam contracts: **pin, gen-check, copy-as-value**.
  [Memory_Model §7]

### D4.6 Name identity

- **D4.6.1*** One address per semantic property key: `NameRef` /
  `PropertyKeyRef` compare by **pointer equality**; `NameId =
  [pool16][ordinal16]` is identity, `SectionNameId = [slot16][offset16]`
  is a **location, never an identity**. Fixed pools (0 markup, 1 Lambda,
  2 JS/DOM) are **generated from one data module, never hand-maintained**;
  hot native comparisons are plain integer compares with no pool lookup.
  [NI1–NI4, NI16]
- **D4.6.2*** Evolve NamePool, don't replace it (first definer wins,
  zero-copy adoption); the FNV `ShapeEntry.name_id` is **not** a NameId —
  renamed `name_hash`, demoted to hash-only; per-module property-key GOT
  with MIR-baked dense indices; **Mark/Input never allocates NameIds for
  document data**; `eval`/REPL use the same pipeline — one pipeline, no
  special cases. [NI6, NI7, NI10, NI12, NI13]

### D4.7 Const pool / MarkPack

- **D4.7.1*** **One binary encoding for Lambda data, four consumers**
  (const pool, documents, interchange, messages): four sections
  (names/types/data/code); containers materialize a spine and map leaves
  (in-place relocation rejected — per-isolate refs can't live in shared
  pages); materialized templates are permanent COW sources; an emitter
  whitelist is the only legal MIR-operand surface. [CP2–CP7, CP25]
- **D4.7.2*** Two stream variants over one encoding (bare stream;
  + trailing skip table and block index) — **sealing appends, stream
  bytes are never rewritten**; two-tier validation (full structural
  before any mapped pointer escapes — *a lying skip table must not
  mis-drive scanners*; magic/hash for trusted caches); malformed input is
  an error, never UB. Functions, closures, VMaps, and native handles do
  not serialize — encoder error. [CP18, CP20–CP22, §4c]

## D5 Execution State: Stacks and Rooting

### D5.1 The stack model

- **D5.1.1** Three stack-like mechanisms with distinct owners: the native C
  stack (invisible to GC); **watermarked side stacks** — the root stack,
  precisely scanned `[base, top)`, and the **number stack, never
  scanned** — two strictly separate mappings; and heap async frames
  (Item region root-registered, tail never scanned). *LIFO lifetimes live
  on the side stacks; non-LIFO lifetimes own their scalars in tail regions
  of their own allocation.* [SF2, SF12, Stack_Frame §5.1]
- **D5.1.2** Static per-function frame sizing, no `MIR_ALLOCA`, two saved
  watermarks per frame, virtual reservation + demand paging (decommit
  after GC), explicit frame-entry limit checks; no hotness detection —
  primitives are unconditionally cheap. *Be as static as a dynamic runtime
  can be.* [SF5–SF7, SF13]
- **D5.1.3** **Downward flows never re-home; only upward flows and
  outliving stores do. Suspension is a re-homing barrier** — an async
  frame is a tail-bearing heap container; LIFO state goes to side stacks,
  non-LIFO to owned storage. [SF19, SF20]
- **D5.1.4** Cleanup order is load-bearing: all `MAY_GC` cleanup runs
  **before** root/number watermark restoration; one epilogue per
  function. Frame-capacity failure is fail-closed through the language's
  failure lane. [Stack_API §8.6, §8.10]

### D5.2 Scalar homes

- **D5.2.1** Function scalar returns use **caller-donated canonical
  homes**: the caller passes a liveness-colored home address as a hidden
  final ABI operand; the callee copies the payload, retags, and restores
  its complete number extent. Space is bounded by **peak simultaneous
  liveness, not call count**. (Supersedes SF14 two-lane returns and
  callee-frame donation.) [Stack_API §8.7]
- **D5.2.2** Scalar homes are raw payload words, never GC roots, colored
  in a separate slot space from root slots; fully-unobserved lanes use a
  discard home; tail calls forward the incoming home. Destination-owned
  scalar storage exists at every ownership boundary (array tails, typed
  fields, envs, module tables, async frames, task/message records).
  [Stack_API §15.3, inv. 20–22]

### D5.3 GC rooting

- **D5.3.1** Generated code uses **safepoint-current canonical slots**: a
  stable home per rootable binding plus colored scratch homes; registers
  are a write-back cache between safepoints. *Root stores are
  proportional to dirty live homes at `MAY_GC` boundaries — not to
  instructions.* Zero-root functions elide the frame entirely. [CR1, CR3]
- **D5.3.2** **GC begins only inside a `MAY_GC` call** — no asynchronous,
  signal-driven, or background collection without stack maps. `MAY_GC` is
  the default; `NO_GC` is a claim **mechanically verified transitively**
  (frozen allowlist checker). [CR4]
- **D5.3.3** C/C++ helpers use slot-backed `RootFrame` / `Rooted<T>` /
  handles / `PersistentRooted` on the same side-root stack: arguments are
  caller-rooted borrows; returns are legal (no safepoint between frame pop
  and caller store); statics use persistent roots; handoffs stay
  continuously rooted. *Generated code writes back, native code writes
  through — one stack, one safepoint invariant, one collector interface.*
  [CR2, RH1–RH8]
- **D5.3.4** Rootability is driven by the four representation classes
  (`BOXED_ITEM`, `RAW_GC_POINTER`, `NON_GC_SCALAR`,
  `RAW_NON_GC_POINTER`) — never by MIR register type. Rooting policy and
  final store insertion live **only in `MirEmitter`**; transpilers report
  semantic events; a guest is precise iff it emits through those
  primitives. [CR5, CR6, SF11]
- **D5.3.5** Argument passing reserves each function's **maximum
  lexically-overlapping argument arity as a fixed suffix of its side-root
  frame** (per-call dynamic rooting measured at +43% on `fib` and
  removed); fixed global Item storage uses epoch-guarded root ranges.
  Cross-semantic context-stack merges are rejected, as is any merge
  involving the number region. [Stack_API §17]
- **D5.3.6** Recovery frames are **TLS-LIFO per boundary** (execution
  entry, task poll, handler, module transaction, guest entry), each with
  an exact side-root + number-stack checkpoint; landing restores the
  watermarks before inspecting the fault; the outer barrier retires
  abandoned inner chains. (Supersedes the single shared recovery point.)
  [Rooting §2.1a, ER]

### D5.4 Runtime globals and EvalContext

- **D5.4.1** **One canonical long-lived `EvalContext` per isolate; exactly
  one sanctioned TLS root.** The hidden `Context*` argument belongs only
  to generated MIR; native helpers read TLS; an eval thread has one
  context identity for its lifetime — no save/bind/restore, migration
  only via quiescent handoff. [RG0, RG1]
- **D5.4.2** Context state lives in lazily-allocated opaque capsules;
  the JIT-visible `Context` prefix is frozen; capsules never reallocate
  (rooted-Item address stability). Root registration takes an explicit
  heap/context and happens at construction, never in the repeated store.
  [RG2, RG5]
- **D5.4.3** **No context-dependent value at a code-baked address**:
  module globals live in per-context slabs indexed by baked slot +
  module cell; IC cells in per-context slabs; shared MIR contexts are
  sealed eagerly. *Reset becomes construction* — reset/replace-heap/
  destroy are three distinct ordered contracts. [MT2 contract, RG11]
- **D5.4.4** Frozen registries stay global (immutable after publication);
  diagnostics may never feed semantics; one event loop per context;
  thread infra stays `__thread`, donated into context fields. Lambda
  script has **no module-scope mutable binding** (structural, §D7.2);
  guests are exempt. **No lock, atomic RMW, or coherence op may be added
  to any repeated execution path.** [RG6–RG10, RG13, RG14]

## D6 Function

### D6.1 The effect coloring

- **D6.1.1** `fn`/`pn` is a one-bit, declared, compiler-checked effect
  system (semantics §S12.1), realized as **infrastructure, not
  documentation**: purity is a core AST flag (guest builders mark
  all-effectful), and every callable surface carries the bit — sys-func
  registry rows, Jube module signatures (the `fn`/`pn` prefix is
  mandatory — *purity is never accidental*), and the runtime-function
  catalog. [U14, NM §6.2, Lang_Hosting §7.1]
- **D6.1.2** The bit is load-bearing across the runtime: it is the
  const-folder's soundness gate, the stream-fusion and parallelization
  license (*Polars/DuckDB must trust their UDFs; Lambda verifies*), the
  reduction-legality condition, the guarantee that `fn` regions are
  suspension-free (no scheduling polls in pure code), and what makes the
  JS Promise membrane computable. **No reified effects**: a `pn` call
  executes; there is no held, unexecuted effect value. [U26, PD11, K19, K23, Features §3.6]
- **D6.1.3*** Effect analyses beyond the bit are separate compiler
  products: `may_await` (scheduling) and `may_defect` (fault
  capability) — and a **missing analysis must mean defect-capable**,
  never "trusted clean"; the reverse polarity was a measured live
  divergence. [IEH §5.3, ER-D13]

### D6.2 Function values and closures

- **D6.2.1** A function value is a GC object carrying its entry pointers,
  its attached type/signature, and its closure environment. Identity is
  the **static AST definition site** stamped at creation — never a memory
  address (addresses break under COW and JIT recompilation; semantics
  S5.5.1). The boxed `_b` entry is the universal dynamic-call target
  (§D8.3). [C8.7, DF7]
- **D6.2.2** Dynamic calls dispatch through **per-callee `fn->invoke`
  entries**; the 16-slot source-argument limit is checked statically
  where possible with a runtime backstop; dynamic calls with named
  arguments, and dynamic calls to `var`/inout signatures, are rejected by
  design. Adapter spans are dynamically sized, precisely rooted,
  LIFO-destroyed (`padded_args[32]` retired). [Function_Arg, LC call-ABI]
- **D6.2.3** Closures are **immutable values**: captures snapshot at
  creation and are stored as `Item`s in the env, unboxed on access;
  assignment — including interior mutation — through a captured name is a
  compile error (semantics S9.1.4). State never lives inside a function
  value. [C4, Box_Unbox]
- **D6.2.4** The closure env is precisely traced (`closure_field_count`);
  wide scalars live in the env's tail region (SF18). Closures are
  **boxed-only** in the dual-func plan — the most valuable future
  relaxation (§D8.3.4). [GC2 §8, DF11]

### D6.3 Concurrency runtime

- **D6.3.1** Tasks share the context heap under **one scheduler per
  context**; a Lambda resume is a macrotask in FIFO readiness order,
  never interrupting a JS job or its microtask checkpoint. The K13
  capture rule (no `var` capture by reference into `start`) is enforced
  at compile time, which is what makes thread count semantically
  unobservable. [K16, RG8, K13]
- **D6.3.2*** Workers are **share-nothing isolates** (thread default,
  process optional); messages mean copies — read-only structural sharing
  is representation only; the `is_shared` COW bit doubles as the
  cross-isolate discriminator. Thread-mode prerequisites: the per-isolate
  runtime-state audit and the cross-isolate lifetime ruling (DO20).
  [K31/K32, CW3-C]
- **D6.3.3** Faults are TLS-scoped — no jump ever crosses a thread
  boundary; a worker publishes only an already-materialized fault result;
  recovery frames never survive a scheduler yield (semantics S7.11).
  [ER-D1, ER-D11]

### D6.4 System built-ins

- **D6.4.1** Public sys funcs return **boxed `Item`** at the runtime/JIT
  boundary; collection results ride `Item.array`/`array_num` (no raw
  pointer convention); `return_type` describes the stable successful shape
  as narrowly as the type system permits — `TYPE_ANY` never justifies
  ad-hoc null/array/error switching. Empty results preserve container
  mode (a typed empty, never a generic array). [Sys_Func §7]
- **D6.4.2** Raw `-1` helpers are **adapter-only**; public wrappers guard
  error operands first, then normalize negatives to `null`; no
  Lambda-surface path calls a `_raw` helper. The §8 registration
  checklist is normative for every new sys func. *The COW writeback value
  and the language-level expression result are different concerns.*
  [Sys_Func §7–§8]

## D7 Modules and Packaging

*Terminology.* Lambda script modules are **packages** where disambiguation
matters; Jube/native DSOs are **modules**. "Module" is nonetheless used
loosely across the corpus — context disambiguates, and we live with it.

### D7.1 Build packaging and layering

- **D7.1.1** Build packaging is **static libraries** (dynamic is reserved
  for Jube modules): four archives + exe — `lib` → `lambda-data`
  (core + io) → `lambda-rt` → `radiant` → `lambda.exe` — with a strict
  leftward-only layering DAG; upward calls only via lower-owned hook
  headers with null-safe defaults. Exception: **GC allocation is a
  safepoint, not an allocator flavor** — a missing heap service never
  degrades to a weak/null/malloc fallback. [SM1–SM3, SM6]
- **D7.1.2** **All resource IO lives in `lambda-io`**: bytes and files
  cache below; **derived artifacts live above** (rt: script/MIR caches;
  radiant: font/media rendering — *Radiant never fetches*). Raw
  context-free mechanisms stay `lib`; event-loop/Item-coupled language IO
  stays rt under an explicit reviewed classification, not a loophole.
  [SM12]
- **D7.1.3** **Provider ownership beats include ownership**: every function
  a module's public header declares is defined by that module or a lower
  one; enforced by symbol-provider inventory. Boundary enforcement is
  two-tier: lint rules + five boundary DSOs that must link
  fatal-undefined-clean; the rt→radiant residue (Class F) is a ratcheted
  import baseline — additions fail, deletions are progress.* [SM13, SM5]
- **D7.1.4** Headless is **two profiles**: profile A is a link-time omission
  of `radiant.a`; profile B a runtime flag inside radiant — same archive,
  no `#ifdef`-stripping. Radiant sits **above** rt (layered, Option A)
  and embeds it via a narrow `embed.h`. Public headers never define
  build-profile macros. [SM9, SM10]
- **D7.1.5** Mark API ownership: `MarkReader` is core; `MarkBuilder` /
  `MarkEditor` are io (they take `Input*`) — ownership resolves the
  layering, not callback abstractions; forwarding shims are deleted once
  call sites migrate. [SM §9.4]

### D7.2 Script packages

- **D7.2.1** A Lambda script module (package) is the compilation unit:
  its language profile is a unit property (D8.2.1), its bindings are
  **immutable at module scope** (no module-scope mutable binding —
  structural, via scope guards; guests exempt), and its instantiation is
  effectful even though the bindings are immutable. Package state lives
  in per-context slabs, never at code-baked addresses (D5.4.3). [RG14, U2]
- **D7.2.2** Imports resolve to the **boxed `_b` symbol** of exported
  functions (export ⇒ boxed entry mandatory, D8.3.4); package
  initialization is a **transaction barrier** — a failed init rolls back,
  and a module with half-established top-level state must never become
  importable (semantics S7.7.6). [DF15, ER-D2]
- **D7.2.3** Imported packages are cached in-process (L1, D8.5.1) and
  distribute as **source** (D1.7); compiled artifacts are derived local
  caches only.

### D7.3 Jube modules: the module system

- **D7.3.1** Core = ECMA-262 + engine substrate + `./lib` + a **closed
  globals allowlist** (console, timers, `queueMicrotask`); everything
  else is a Jube module; extending the allowlist is an ADR-level change.
  Core keeps file I/O + the shared stream core **for performance**;
  spec-surface stream/crypto APIs are modules. [JA1]
- **D7.3.2** Modules are DSOs + `module.json`: hash-verified, ABI and
  hosted-API negotiated before any module code runs, lazily loaded,
  **transactionally registered** (failed init rolls back),
  process-lifetime (no hot unload), additive-only evolution behind
  size gates. Static registration is dev-only with mandated behavioral
  parity. [JA2, JA5, JA11]
- **D7.3.3** **One strict host-API tier for everyone** — `jube.h` only, no
  second softer tier for in-tree modules (*in-tree modules are the
  constant test of the same contract third parties get*); no host-internal
  symbol imports; the v1 surface is **derived by extraction from real
  usage, not speculation**. Signatures are **Lambda type-syntax strings**
  with mandatory `fn`/`pn` (D6.1.1). [JA5, NM §6.2, §6.4]
- **D7.3.4** Core never links modules; modules never link each other's
  symbols (manifest dependencies or script re-export instead); the
  manifest is readable without dlopen and mirrors the C descriptor table,
  which is ground truth. [JA8, NM §7]
- **D7.3.5** **Every module kind names its conformance gate, wired into
  CI**: core = Test262; web-platform = its WPT slice; node-compat = the
  node baseline; languages = their own corpus; data packs = pack corpus +
  integrity. [JA9]

### D7.4 Jube modules: data and async contracts

- **D7.4.1** `Item` is the only value currency; native structs cross as
  **VMap projections** (brand = vtable identity; finalize = sweep-time
  destroy; an opaque handle is the zero-field degenerate case); a raw C
  pointer never appears in a script-visible signature; system resources
  are integer **rids**. Principle: **MapKind = ECMAScript-spec exotics,
  engine-owned; VMap = host/native objects, module-owned** — the DOM
  MapKind special case is deleted.* [JA6, NM §6.3, §8]
- **D7.4.2** Modules are **fully shielded from the async substrate** — no
  loop escape hatch, ever. A micro-kernel of five concepts (scheduling,
  thread-safe completion post, blocking pool, rid table, shutdown);
  completions delivered only on the JS thread in loop order; *modules
  never pump, poll, or drain*. [JA7]
- **D7.4.3** Guest hosting crosses **no core types**: no `Runtime*`,
  `EvalContext*`, `Input*`, `MIR_context_t`, or `AstNode*` over the
  boundary — opaque handles and versioned sub-API tables only. Every
  callable runtime helper carries catalog metadata (signatures, ownership,
  GC/allocation effects, re-entry, exception behavior); lowering resolves
  through the catalog, never by guessing. The host owns heap activation,
  context install, side-stack entry/exit, and recovery boundaries —
  guests never construct contexts or touch global runtime pointers (the
  G1 gate). JS pending-exception behavior is not reused as another
  guest's exception model.* [Lang_Hosting §5–§7, §13]

### D7.5 Jube modules: trust and IO

- **D7.5.1** Three trust tiers: **T1 system** (in-process DSO — *not a
  security boundary*: loading one ≡ linking it into the host), **T2
  confined** (separate process **+ OS sandbox + capability broker — all
  three, or it means nothing**; a second interface style, not a loading
  option), **T3 sandboxed WASM** (enforced by construction, the
  recommended default for untrusted code). **Third-party native modules
  never enter the runtime process.** T1 admission carries a
  thread-isolation-safety declaration; undeclared ⇒ process-isolation
  only, thread-isolate load is a load-time error.* [JA10]
- **D7.5.2*** **One central IO API is the sole IO door** for Lambda,
  Radiant, and all modules — raw IO isolated to `./lib`; one chokepoint,
  not two; it carries **policy** (path/capability gates, isolation,
  audit, quotas), a real boundary for scripts and governance for T1.
  *Converts "did someone sneak in an `open()`?" from an unanswerable
  question into a checker rule.* Surface extracted empirically. [JA16]
- **D7.5.3** Lambda↔Radiant is two explicit contracts: Radiant embeds
  Lambda through a versioned, Jube-aligned embed interface; Lambda
  reaches Radiant only through the `radiant-dom` module; `lambda-rt`
  never links radiant. [JA15, SM10]

## D8 Compilation Pipeline

### D8.1 Structure

- **D8.1.1** Tree-sitter grammar → typed AST → **MIR Direct**
  (`transpile-mir.cpp`) → MIR JIT. MIR-interp is the sole non-JIT
  execution path; there is **no AST-walking interpreter** — a const-folder
  over the pure core subset instead (*possible ≠ needed*); a reference
  interpreter is KIV for spec purposes only and never wired into eval.
  [U26]
- **D8.1.2** Grammar is regenerated from `grammar.js` (never hand-edit
  `parser.c`); build config generates the build files (never hand-edit
  the Lua). [rules 5, 7]

### D8.2 Unified AST

- **D8.2.1*** One leveled core-node catalog serves host and guests; ≥80%
  of any guest lands in core nodes. Language is a property of the
  **compilation unit** (`Script.lang → LangProfile*`), never of a node;
  operator semantics dispatch through the profile. Mixed-language
  *programs* exist; mixed-language *modules* do not. [U1, U2, U4]
- **D8.2.2*** Variance tiers in strict preference order: fields/flags →
  clause chains → language-range node kinds; **promotion to core allowed,
  demotion never**. *Semantically unified, syntax variance preserved as
  fields*: one LOOP/MATCH/IF/ASSIGN node with form fields, **no tree
  rewriting** (desugar hazards never arise). `AST_FOR` is its own core
  node (pure `fn` iteration vs `pn` condition loops — different color);
  purity is a **core flag** and the const-folder's soundness gate. [U3, U19, U22, U14]
- **D8.2.3*** Migration is extract-after-convergence: the common core is
  extracted only after two working clients exist; the G1 rooting fix is a
  Phase-0 prerequisite baked into `MirEmitter`; Python is the first guest
  port and the acceptance test. [U12/U21, K17 doctrine]

### D8.3 Dual-function compiling

- **D8.3.1** Plan-dependent entries: `<name>` unboxed + `<name>_b` boxed,
  roles set per function by the planning matrix; exactly **one unboxed
  version per function** (multi-version specialization is future work);
  per-param partial specialization rejected. A declared, native-eligible
  function has **no boxed slow body**; a slow body exists iff a param is
  inference-specialized, unboxing is profitable, and the caller set is
  open. [DF1–DF3]
- **D8.3.2** **The check lives in the callee**: a site that cannot
  statically prove admission calls `_b`, and the callee does the one
  boundary operation — *one guard site, one correctness argument*.
  Declared violation → TE-9 error value; inferred mismatch → guard fails →
  boxed body. Inferred guards are **exact semantic-shape tests**, not
  admissions; unions/`any` get no test and stay `Item`. [DF4, DF5, DF8]
- **D8.3.3** **Source-relative correctness is the invariant**: neither
  generated body defines the other's semantics; guard-failing inputs must
  never reach the unboxed entry; the unboxed entry has exactly **two**
  proof-producing paths (statically-proven site, or `_b`) — *any third
  path is a bug, not a slow path*. [DF9, DF14]
- **D8.3.4** **Visibility decides which versions exist**: exported ⇒ `_b`
  mandatory; non-exported with one exact visible call shape ⇒ unboxed
  only, no guard, no slow body. Caller-side guards are accepted **only as
  guard hoisting** (same predicate, failure edge lands on the `_b` path,
  flag-gated, benchmark-justified).* Boxed-only exclusions: closures,
  methods, variadics, `pn`/`var` params, may-await procs. [DF15, DF16, DF11]
- **D8.3.5** Numeric admission at implicit boundaries is one rule (static
  whole-domain embedding admits + normalizes; dynamic admits iff lossless;
  admission ≠ cast); entry-shape inference and body inference are separate
  products (D3.3.2). [DF6, DF12, DF13]

### D8.4 Dispatch policy

- **D8.4.1** **No inline caches anywhere in the Lambda lane** —
  *specialization over caching*: specialized lowerings have no
  dynamic-dispatch sites by construction; open sites use closed-switch
  runtime dispatch; a hot open site is a DF12 inference bug first.
  Multi-version dispatch, when it comes, is a guard chain, never a
  patchable cache. *Lambda's dispatch space is closed; JS's is open* —
  LambdaJS keeps its ICs (guard-based cache cells beside immutable MIR,
  never patched instructions). Generated code stays immutable — an
  architectural asset the cache tiers rely on. [LC1]
- **D8.4.2** Core direct calls pass individual ABI operands (`Context*`,
  args, optional trailing scalar home); `Item* + argc` is the JS dynamic
  boundary only. [LC call-ABI]

### D8.5 MIR module cache

- **D8.5.1** **L1** (landed): persistent in-process cache of imported
  modules only, keyed by canonical path + mtime/size; name pool promoted
  to batch lifetime; module indices never compacted. **L2** lazy codegen
  is an approved experiment.* [MC1, MC2]
- **D8.5.2*** The disk-cache direction is **code image + relocation
  journal** (Route B); a C-source route is rejected (a C toolchain as a
  production dependency), and MIR-bitcode caching is dropped (*it caches
  the wrong phase*). Prerequisite: **de-pointering** the lowering — baked
  pointers are anonymous immediates with no relocation records, so
  value-based identification is unsound. v1 load model regenerates the
  front-end and loads the back-end. [MC3, MC5/MC6, L3-1..L3-2]
- **D8.5.3*** The **differential write verifier is mandatory and
  fail-closed**: compile twice at different addresses, diff; any byte the
  journal cannot explain vetoes caching that module — *every cache entry
  is born verified*. Keys include build stamp, arch, opt policy, format
  version, and transitive source hashes; artifacts are local,
  SHA-validated, size-capped, never signed, **never distributed**.
  Per-instantiation mutable cells (JS ICs) must become named data items
  before JS joins. [L3-6..L3-9, MC7/MC8]

### D8.6 Emission testing

- **D8.6.1** **The MT7 ratchet: 0% slack.** Any emission-size increase
  over `test/mir/mir_budgets.json` fails the baseline; landing growth
  requires a manual threshold lift **in the same commit** — *the budget
  diff is the review artifact*. Decreases auto-tighten; the test never
  edits its own config; nondeterministic probes are removed, not padded;
  no silent platform-dependent slack. [MT7]
- **D8.6.2** One artifact contract: the MIR dump path honored in release;
  `--no-log` is the master switch, and mandatory emitter aborts stay live
  under it. `mir-check` sidecars assert names and instruction shapes,
  **never raw immediates**, over the finalized dump. [MT1, MT2]
- **D8.6.3** Liveness is verified by **dynamic oracles** — forced-GC
  (`FORCE_EVERY=1`) + poison sweeps, self-baselining (stressed run must
  byte-match unstressed), gated in the baseline; a static liveness
  duplicate is rejected. The independent structural verifier is deferred
  until emission is final. [MT4, MT3]

---

## Appendix A — Implementation Footnotes

Status of `*`-marked rulings as of 2026-08-06.

| Ruling | Status |
|---|---|
| D2.1.6 | Guardrail layer partial: ~24 raw `>> 56` sites across 11 files, open-coded `get_double` derefs, raw `MIR_EQ` emissions outstanding. |
| D2.3.2 | Container unbox helpers + `p2it` returns designed, not landed (Box_Unbox2 Phase 1); MIR path still boxes container params as ANY (safe, unoptimized). |
| D2.4.1–D2.4.3 | `MirValue`/`em_require_rep` infrastructure exists; LambdaJS uses it; propagation through Lambda expression lowering not started (phases L0–L6), sequenced after nullable-lane work. |
| D2.5.1 | Nullable-lane first slice landed 2026-08-05 (LaneStorageDesc, native arrays, packed nullable fields, scalar ABI); `f16?`/`f32?`, JS IC lowering, mutable ArrayNum views, vector/N-D kernels pending. |
| D2.6.2 | ArrayNum `==` representation-sensitivity is a known live bug (also gates the data-processing engines). |
| D2.6.3 | ELEM_INT i64 revert landed; SIMD kernels only partly re-enabled (C16-era gating comments remain). |
| D2.7.2 | Ownerless-slot GC scalar fallback active and counted; removal gated on the per-boundary inventory reaching zero. SG2 OQ audits open (dispatch-helper enumeration, resume-path slot reads, RetItem census). |
| D3.1.1 | `Type*` kind-discrimination is code-authoritative only — no design record owns the first-class type-value representation (DO22); the type-graph de-pointering census is deferred to its own doc (CP §6 census C). |
| D3.2.2 | Constrained-type enforcement is base-only; the `is`/`fn_is`/validator three-way divergence is open (TE-6 P5). |
| D3.4.3 | Shape pool shipped for `Input` (contrary to its doc's stale "planning" header); the runtime/EvalContext shape pool (Shape_Pool Phase 5) is not implemented — runtime maps rebuild per transition instead of interning. |
| D4.2.2 | Memory Context Stage 1 implemented and verified; Stage 2 (page allocation, OOM ladder) not started. |
| D4.4.3 | COW Stage 1 landed 2026-07-23; Stage 2 (exclusivity faces, view confinement, module-`var` rule, snapshot iteration) deferred, designed. |
| D4.6 | Name identity is a PROPOSAL (rev 5): W1/W2 integer schemes can start now; W4 stage 3 blocked on the MIR-cache reconciliation (NI §8). |
| D4.7 | Const pool / MarkPack is a DRAFT (rev 4): baked-pointer census verified against emitters 2026-07-31; phases CP-P0..P4 not started. |
| D6.1.3 | `may_await` analysis exists; the `may_defect` split does not — `may_return_error` is overloaded and the missing-analysis polarity is currently "trusted clean" (wrong direction; one half of the measured O1 divergence). |
| D6.3.2 | Worker tier pending entirely: process isolation first, thread isolation gated on the isolate-state audit and DO20. |
| D7.1.3 | Static modules implemented (rev 29, P0–P6) except Class F: the rt→radiant boundary is a ratcheted 165-import baseline; P1c constructor consolidation deferred. |
| D7.4.1 | Native-module POC 1 (radiant-dom onto VMap, MapKind deletion) not started; the JS semantic-adapter items 1–8 must be specified before its step 3. |
| D7.4.3 | Hosted-language layering: `lang-python` is the landed DSO reference chain, but Python is currently statically linked and its ten follow-up ADRs (Lang_Hosting §17) are unwritten. |
| D7.5.1 | T1 verification layers staged; T2/T3 directional, neither built (not required until a third-party module story). |
| D7.5.2 | Central IO API direction adopted; surface not extracted (`js_fs`/`js_os`/`js_net` raw-IO violations are the burn-down list); `dynamic_lookup` laxity is acknowledged debt. |
| D8.2 | Unified AST design settled (U1–U26); phased migration not recorded as started; Python port is the acceptance test and its entry is disabled until it emits through `MirEmitter` (CR6). |
| D8.3.4 | DF16 guard hoisting decided, flag-gated, unimplemented (P7); DF12 speculative lifting deferred (P5); §10 multi-version specialization future; the size-gate threshold unset. Dual-func Stage 1 core (P0–P4, P6) complete. |
| D8.5.1 | MIR cache L1 landed; L2 lazy codegen approved but `mir.c` still eager. |
| D8.5.2–D8.5.3 | L3 code-image cache: nothing landed (D0–D6 sequence); de-pointering (MC4) independently shippable, not started. |

## Appendix B — Open Design Issues (DO#)

Numbered `DO#` (design-open); each links to its record.

**Data representation**
- **DO1** Nullable-lane residue: boxed fallback for `any`/`number`/unions
  (confirm); absent-vs-null-valued optional field observability;
  flow-proof scope; native `pn` write-back ABI; ArrayNum re-promotion.
  [Nullable §10]
- **DO2** Scalar-home audits: result-bearing dispatch-helper enumeration;
  `start`/await resume slot reads; exception-peek rematerialization;
  RetItem-vs-Item ABI census; donated-home lifetime proofs. [SG §5]
- **DO3** Box/unbox: union params stay boxed (revisit?); return-type
  participation in unboxed-call selection; varargs policy.
- **DO4** ValueRep: `MirValue` size (measure first); whether
  `FnValueAnalysis.actual_rep` becomes the planned-occurrence source or is
  deleted — no second analysis ledger. [Lane §9]

**Memory & GC**
- **DO5** Numeric GC-fallback end state: per-boundary inventory to zero,
  then delete `scalar_heap`; any future donated-persistent-home protocol
  needs explicit owner/lifetime and a borrowed-vs-retained public ABI.
  [Stack_API §15.5–15.6]
- **DO6** Side-root slot metadata (boxed-Item vs raw-pointer tag) — only
  needed for a moving collector; `Rooted` cached-raw-copy idiom for hot
  native loops. [Rooting CQ6/CQ7]
- **DO7** Stack maps (KIV) — the only route to zero mutator rooting cost;
  requires forking the MIR backend. Moving/generational collection stays a
  non-goal; exact mutable homes are prerequisite infrastructure only.
- **DO8** CW4 shared-flag → saturating count/GC-refresh: blocked on CW5
  counters from a live corpus; element chunked-children representation for
  huge fan-out gated on the editor benchmark.
- **DO9** Memory Context Stage 2 opens, incl. measuring MIR JIT code-page
  bytes; RG G1 defect: `push`/`splice` bypass E211 on a module-level
  `let`, breaking RG14 and the `fn`-purity claim.
- **DO10** SF verification items: concat memcpy without tail-payload
  rebase (latent dangling wide scalars); temporaries live across a
  mid-expression `wait`; closure-env precise-tracing layout split.

**Compilation**
- **DO11** Dual-func: O1 promotion-aware numeric inference and lowering
  (Stage-1 boundary only today); O6 guard-failure counters; O14 cold
  boxed bodies still JIT at startup — lazy slow-path generation belongs
  with the cache work; the DF12 "SPECULATE INT" revisit.
- **DO12** MIR cache: GC-root registry reset under L1 (verify before
  relying); thunk identity under lazy gen; name-pool growth bound
  (never reset — needs a high-water metric); L3 opens (Lambda
  `expr_data` production, `MIR_link` NULL-interface behavior, x86 rel32
  survivorship, Model-A hit-path cost, main-script caching,
  per-function granularity).
- **DO13** Emission testing: guest-transpiler scope (deferred to the
  Python frame port); MT6 coupling spike; a clean zero-root-elision
  fixture. MIR-Direct lacks all of C2MIR's sys-func specializations
  (`fn_pow_u`, native `sin`/`sqrt`) — deliberately pinned by a forbid
  fixture; porting them is a decision, not a drift.
- **DO14** Unified AST: the "views" fourth variance tier (adopt only when
  a real shared pass asks); C2MIR retirement revisit after Phase 4.
- **DO15** Online exception-poll doc status conflict: the impl doc is
  named `(done)` but reads PLANNED (E0–E6 future) — resolve before citing
  OE1–OE10 as landed.

**Runtime services & modules**
- **DO16** Name identity: temporal canonical accepted; dynamic-intern
  growth bound (KIV); pooled-flag audit; GOT rebuild on L1 in-memory
  cache hits must not be skipped; mapped-backing granularity; and the
  whole **MIR-cache reconciliation** (NI §8) — settled jointly with const
  pool on String/NameMeta ABI versioning and NameId-vs-SectionNameId
  reference tagging.
- **DO17** Const pool: decimal sharing scope; datetime packing;
  cross-module literal dedup (KIV); mapped-leaf page retention; the
  eager-materialization benchmark (gates the document path); serializer
  heuristics; per-block compression (KIV); incremental-append semantics.
- **DO18** Sys-func registry: the public mutation convention (owner-
  returning vs unit) and `splice`'s public result; the confirmed ArrayNum
  carrier violations (§6.2) unfixed.
- **DO19** Jube: `js_globals.cpp` allowlist audit; WPT harness choice
  (before `web-streams`/`web-crypto` land); data-pack design (first
  adopter); `dynamic_lookup` retirement; the Radiant-domain host API and
  the Lambda↔Radiant embed contract; the IO policy model (gate taxonomy,
  realms, audit format); T2 marshaled interface + per-platform sandbox
  profiles; JS semantic-adapter items 1–8 before POC-1 step 3;
  owning-vs-non-owning `JubeTypeDef`; Windows loading parity; the ten
  Lang_Hosting §17 follow-up ADRs.
- **DO20** Cross-isolate lifetime for shared graph Items (promote-on-share
  recommended) — must be decided before thread-mode workers; prerequisite
  is closing the C4.1 aliasing catalog. [Concurrency O-D]
- **DO21** Static modules: Class F scheduling (the browser-style
  engine/bindings split is the eventual shape); profile-B headless audit;
  the §2.2 runtime-native IO inventory completion; Windows per-module
  export macros.

**Type & shape**
- **DO22** First-class `Type*` representation needs its own design record:
  today the kind-discriminated layout is code-authoritative only; union
  normalization's canonical ordering is unpinned; the type-graph
  de-pointering census (CP §6 census C) is deferred to the same future
  doc (`§2-types` is its designated home).
- **DO23** Shape system opens: hash-collision policy (signature-only
  lookup vs deep compare as authority); the element-name-as-synthetic-
  attribute-zero encoding (bless or replace); cross-Input and
  parse-vs-runtime shape interning (Shape_Pool Phase 5) and shape
  serialization; `CachedShape.ref_count` semantics (declared, always 0 —
  shapes are effectively arena-immortal); state explicitly that
  `byte_offset` is derivable and excluded from identity.

## Appendix C — Decision-Record Index

| Section | Records | Where argued |
|---|---|---|
| D1 | JA4/JA13, J5, P1–P10, LC/Lane/MT preambles, rules 5/7/14 | `Lambda_Design_Jube_Architecture.md`, `Lambda_Design_Jube_Lang_Hosting.md` |
| D2.1 | Item_Boxing §0–§8 (R7–R10, W1–W3) | `Lambda_Design_Item_Boxing.md` |
| D2.2 | Double_Boxing; Int_Type §5.1; Stack_API §15 | `Lambda_Type_Double_Boxing.md`, `Lambda_Semantics_Int_Type.md`, `Lambda_Design_Stack_API.md` |
| D2.3 | Box_Unbox, Box_Unbox2 | `Lambda_Box_Unbox.md`, `Lambda_Box_Unbox2.md` |
| D2.4 | Lane §1–§9 | `Lambda_Design_Compiling_Lane.md` |
| D2.5–D2.6 | Nullable §1–§10; CW16 | `Lambda_Design_Compiling_Nullable.md`, `Lambda_Design_Runtime_COW.md` |
| D2.7 | SG1–SG8 | `Lambda_Design_Scalar_GC_Invariant.md` |
| D3.1–D3.3 | C8.5-4, C9a; TE-1/TE-6/TE-10/TE-13; DF12/DF13; B7; Lane §1 | `Lambda_Semantics_Formal2.md`, `Lambda_Design_Type_Enforcement.md`, `Lambda_Design_Compiling_Dual_Func.md` |
| D3.4 | Shape_Pool §1–§8; Transpile_Map DD1–DD4; NI10/NI13; Nullable §6; TE §6 B7b | `Lambda_Shape_Pool.md`, `Lambda_Transpile_Map.md`, `Lambda_Design_Name_Identity.md` |
| D4.1 | GC1 §2.10.4; CW8; SF16; CR8 | `Lambda_Garbage_Collector.md`, `Lambda_Design_Runtime_COW.md`, `Lambda_Design_Stack_Rooting.md` |
| D4.2 | Memory_Context stages | `vibe/Memory_Context.md` |
| D4.3 | GC2 §4–§12 | `Lambda_Garbage_Collector2.md` |
| D4.4 | CW1–CW21 | `Lambda_Design_Runtime_COW.md` |
| D4.5 | Memory_Model §5–§7 | `Lambda_Design_Memory_Model.md` |
| D4.6 | NI1–NI16, W1–W6 | `Lambda_Design_Name_Identity.md` |
| D4.7 | CP1–CP26 | `Lambda_Design_Const_Pool.md` |
| D5.1–D5.3 | SF1–SF20, OS1–OS11; Stack_API phases + invariants; CR1–CR8, RH1–RH8; Merges A/B/C | `Lambda_Design_Stack_Frame.md`, `Lambda_Design_Stack_API.md`, `Lambda_Design_Stack_Rooting.md` |
| D5.4 | RG0–RG14, MT2 contract | `Lambda_Design_Runtime_Globals.md` |
| D6.1 | U14, U26; Features §3.6; NM §6.2; Lang_Hosting §7.1; IEH §5.3 | `Lambda_Semantics_Features.md`, `Lambda_Design_Native_Module.md`, `Lambda_Impl_Error_Handling.md` |
| D6.2 | C8.7; Function_Arg; DF7/DF11; SF18 | `Lambda_Semantics_Formal2.md`, `Lambda_Design_Function_Arg.md` |
| D6.3 | K11–K32 (runtime side); ER-D1/D11 | `Lambda_Design_Concurrency.md`, `Lambda_Design_Exec_Recovery.md` |
| D6.4 | Sys_Func §7–§8 | `Lambda_Design_Sys_Func.md` |
| D7.1 | SM1–SM14 | `Lambda_Design_Static_Modules.md` |
| D7.2 | RG14; DF15; ER-D2; MC1 | `Lambda_Design_Runtime_Globals.md`, `Lambda_Design_Compiling_Dual_Func.md`, `Lambda_Design_Exec_Recovery.md` |
| D7.3–D7.5 | JA1–JA16; Native_Module §6–§10; Lang_Hosting P/C + §5–§13 | `Lambda_Design_Jube_Architecture.md`, `Lambda_Design_Native_Module.md`, `Lambda_Design_Jube_Lang_Hosting.md` |
| D8.1–D8.2 | U1–U26 | `Lambda_Design_Unified_AST.md` |
| D8.3 | DF1–DF17, O1–O14 | `Lambda_Design_Compiling_Dual_Func.md` |
| D8.4 | LC1 + call-ABI notes | `Lambda_Design_Compiling.md` |
| D8.5 | MC1–MC8; L3-1–L3-10 | `Lambda_Design_MIR_Cache.md`, `Lambda_Design_MIR_Cache_L3.md` |
| D8.6 | MT1–MT8 | `Lambda_Design_MIR_Emission_Test.md` |

The decision records preserve the full deliberations — every alternative
that lost and the arguments that did not persuade. This specification is
their distillation: the record governs the history; this document governs
the design.

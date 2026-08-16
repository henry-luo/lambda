# Lambda Formal Design — Specification

**Spec version:** 1.24.0 (2026-08-15)

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
- **D1.4v2 — Errors are return values at every boundary.** A fallible
  function returns one merged `Item`: its success value or an ERROR-tagged
  error value. No pending-exception side channel, C++ exception, `longjmp`,
  or guest unwind crosses a language or module boundary. [S7.4.4, JA5, J3]
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

### The invariant ledger (DI)

Standing design invariants distilled from the rulings. Each holds in every
build unless marked otherwise; any observed violation is a defect. The
language-visible counterparts are the semantics spec's SI ledger.

- **DI1 — The Item contract.** `sizeof(Item) == 8`, passed by value; leaf
  pointers fit the low 56 bits; raw container pointers stay in the
  supported low range and outside the inline-double discriminator space;
  TBI/MTE top-byte metadata never leaks into an Item. [D2.1]
- **DI2 — Canonical encoding.** One encoding per value per build;
  `get_type_id` is the only semantic-type interface; a raw Item word
  compare is never semantic equality unless canonicality proves it. [D2.1.2, D2.1.5]
- **DI3 — The tag budget is governed.** 64 legal high-byte values under
  inline doubles; claiming one updates the partition assertions and this
  doc in the same change. [D2.1.4]
- **DI4 — Lane sentinels are private.** Int-lane sentinels never escape
  into an Item, container, or guest bridge; an INT-tagged Item always
  decodes to a finite band value. [D2.2.2]
- **DI5 — No sentinel laundering.** A nullable lane value is tested before
  arithmetic, compare, deref, or typed store; a plain-`T` carrier never
  holds a null sentinel; `box(NULL_LANE(T)) == ItemNull` exactly. [D2.5]
- **DI6 — GC sees only pointers.** Numeric/bool sentinels and scalar homes
  are never roots; the number stack is never scanned. [D2.5.1, D5.1.1, D5.2.2]
- **DI7 — No scalar cells in the GC heap.** No `INT64`/`UINT64`/`FLOAT`
  payload is ever a standalone GC allocation; an Item with those tags can
  never point into the GC zone. [D2.7.1]
- **DI8 — ArrayNum is null-free.** Its contract may be `T?`; its storage
  never holds any null encoding — an admitted null store demotes first.
  [D2.6.2]
- **DI9 — Container-owned scalars.** A container's wide-scalar Items point
  only into its own buffer; headers are never reallocated out from under
  an identity. [D2.6.4]
- **DI10 — The safepoint contract.** GC begins only inside a `MAY_GC`
  call; `MAY_GC` is the default and `NO_GC` a mechanically verified claim;
  all `MAY_GC` cleanup runs before watermark restoration. [D5.3.2, D5.1.4]
- **DI11 — One context, no baked addresses.** One `EvalContext` per
  isolate, one TLS root; no context-dependent value at a code-baked
  address; capsules never reallocate. [D5.4]
- **DI12 — Hot paths stay lock-free.** No lock, atomic RMW, publication
  check, or coherence op is ever added to a repeated execution path. [D5.4.4]
- **DI13 — Representation is carried.** Emitted MIR register classes are
  never consulted for semantics, representation, nullability, or
  ownership. [D2.4.1]
- **DI14 — Entry integrity.** The unboxed entry has exactly two
  proof-producing paths, and a guard-failing input never reaches it;
  generated code is immutable — never patched. [D8.3.3, D8.4.1]
- **DI15 — Nothing unwinds across a boundary.** Errors are return values
  at every language and module boundary; no exception, `longjmp`, or guest
  unwind crosses one; faults never cross a thread boundary. [D1.4v2, D6.3.3]
- **DI16 — Transactional initialization.** A half-initialized package or a
  failed module registration is never observable — init commits or rolls
  back. [D7.2.2, D7.3.2]
- **DI17 — One value currency.** `Item` crosses every boundary; a raw C
  pointer never appears in a script-visible signature; compiled artifacts
  are local derived caches, never distributed. [D7.4.1v2, D1.7]
- **DI18 — The emission ratchet.** Budgets carry 0% slack; the commit that
  grows emission carries the budget edit. [D8.6.1]
- **DI19 — The COW bit.** The shared flag is monotonic; a false "shared"
  is a wasted copy, a false "unique" is a semantic bug; the unique fast
  path is one byte load/test/branch. [D4.4]
- **DI20 — Native lanes are error-free.** No `error` value ever occupies a
  native carrier — ArrayNum or native-Array storage, a packed map field, a
  native local, an unboxed entry parameter or return — and no unboxed
  operator ever receives an error operand. A source not provably
  error-free is carried boxed until discharged; a lane store either proves
  admission or does not happen. Gating is static and fail-closed; the
  fault regime is separate. [D2.8]

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
- **D2.7.2v2*** Public entry wrappers speak the **companion-lane pair**
  (D5.2.1v2); the trailing home out-param is retired. Rejections stand:
  callee donation (unbounded in dynamic-call loops), context-global
  *homes*, C-local forwarding — the Windows companion *transport* (a
  transient `EvalContext` slot, dead at every call boundary, never the
  pointee of any Item) is not a home and inherits none of the rejected
  failure modes; ruled 2026-08-14 as the Windows lowering (RVO1/RVO2
  closed — one protocol on every platform). The ownerless-slot GC
  fallback is counted and **explicitly transitional**. [SG2, RV10–RV12]

### D2.8 The error-free lane invariant

*The representational counterpart of the semantics' acceptance rule
(§S7.8.1). What DI5 and DI8 are for nulls, this is for errors.*

- **D2.8.1** **No native lane slot ever holds an error.** ArrayNum and
  native-Array element storage, packed map-field storage, native locals,
  unboxed entry parameters and returns, and the scalar homes behind them
  are error-free **by representation** — there is no Item word to put an
  error in, and no lane encoding is ever spent on one: the int lane's
  out-of-band encodings denote `int.nan`/`int.inf` values (D2.2.2), the
  null sentinels denote null (D2.5.2), and dense sized lanes have no spare
  pattern at all. Both escape hatches are rejected rulings, not unbuilt
  options: an **in-band error sentinel** records only *that* a failure
  happened (contradicting rich error payloads) and re-creates the
  consumer-dependent in-band sentinel that was a measured live divergence;
  a **sidecar** index→error table fixes the write and nothing else,
  because read-back then delivers an error into the lane anyway. A lane
  that must be decoded for error-ness is a boxed Item wearing a native
  costume. [S7.8.1; IEH I1, TE-17]
- **D2.8.2*** **No unboxed operator ever receives an error operand**, and
  the **emitter** guarantees it, not the runtime: no skip target, landing
  pad, or error edge is ever emitted inside a native lane. Scope is
  errors and defects only — system/resource faults are untyped, no static
  analysis can gate them, and they unwind through `LambdaRecoveryFrame`
  (D6.3.3), abandoning any partially-built container. [IEH I2, TE-15]
- **D2.8.3*** **Lane entry is gated on static proof, never on runtime
  rescue.** Admission is read from the destination contract (§S7.8.1): a
  provably infallible source enters branch-free; a source only inferable
  as `T | error` is **not lane-eligible** and stays boxed until
  discharged — never a checked lane store that might fail mid-mutation. A
  declaration's entry guard then dominates every use of the binding in its
  scope (§S7.7.2), so D2.8.1 and D2.8.2 hold **by construction rather than
  by per-use guards**. The gating analysis is `may_defect` with
  fail-closed polarity (D6.1.3): unknown ⇒ defect-capable ⇒ demotion,
  which is viral through unanalyzed callers — so the effect split is a
  **prerequisite** for lane routing, not a later optimization, and an
  unannotated literal that boxes solely for unproven fallibility must be
  diagnosed rather than silently demoted. [TE-17, TE-18; IEH I3, I4]

## D3 Type and Shape

Types are first-class in Lambda — composed, inferred, reflected, and
matched/queried (semantics §S10.1, §S11). This section rules the machinery
that carries them.

### D3.1 First-class type values

- **D3.1.1v2*** A type value is a `Type*` graph node under one compact
  TypeId (`LMD_TYPE_TYPE`), discriminated by `Type.kind` (simple, unary,
  binary, pattern, constrained, range, or parameter). Composite, pattern,
  range, and constrained types share the tag rather than spending tag-budget
  entries (D2.1.4). A range kind carries static bounds for inclusive
  membership while preserving the value's storage domain; a pattern kind
  carries its domain tag and compiled content matcher. The `Type*` graph is
  the **semantic authority** for every contract the compiler carries
  (D2.4.1); a TypeId alone is never a contract.
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
  results; this is a standing invariant (semantics SI3), checked by
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
- **D3.4.4v2** ShapeEntry name identity is one integer field, `name_id`.
  `name_hash` is routing metadata only and never identity; a non-zero
  `name_id` is compared exactly. `NAME_ID_NONE` is reserved for an ordinary
  id-less Input field and is confirmed by key kind, length, and bytes at the
  explicit Input seam. Symbol and private entries never enter that byte
  fallback. Shape copies, descriptor clones, and transitions preserve the
  field unchanged. [NI10, NI13]
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
- **D3.4.7** A runtime JavaScript `TypeMap` may carry one immutable
  `const JsClassMeta*` refinement selected before publication. The metadata
  contains only stable IDs, flags, policies, and static operation-table
  pointers: no `Item`, realm prototype, mutable cache, or context-owned
  pointer. It is orthogonal to D3.4.2's structural field-layout identity but
  qualifies runtime TypeMap, transition, and cache identity; TypeMaps from
  different metadata families are never interchangeable runtime blueprints.
  Foreign/Input TypeMaps may retain null metadata until the explicit JS
  boundary creates a runtime family. [JS_Runtime_Object_Property JOP1–JOP5]

## D4 Memory Management

### D4.1 Allocator tiers

- **D4.1.1v2** Four **content tiers**: **GC heap** (runtime values), **Input
  arena** (parsed documents), **AST/const pool**, **NamePool** (interned
  names). The non-GC tiers are invisible to the collector: `gc_is_managed()`
  (a pointer range test) is the single gatekeeper; tracing stops at any
  non-managed pointer. Content tiers state lifetime and collector
  visibility; *how* heap bytes are obtained and released is the orthogonal
  **mechanism axis** of D4.1.4 — every content tier maps onto exactly one
  mechanism. [GC1 §2.10.4; Mem_Heap §1.1]
- **D4.1.2** Static Mark data is **born shared and immortal**:
  `MarkBuilder` sets the shared COW state at construction; the first
  mutation materializes into the runtime heap. Reads of wide scalars out
  of containers: **reference iff immortal storage (const pool / Input
  arena), else copy** — the discriminator is storage class, not
  mutability. [CW8, SF16]
- **D4.1.3** Input/format allocations are pool/arena-owned and **outside GC
  rooting entirely**; any future GC-heap boundary there requires an
  explicit audit. [CR8]
- **D4.1.4v4*** Heap bytes are kept by exactly **four mechanisms** — tracked
  raw allocation (`memtrack`), GC heap, arena, pool — and no fifth may be
  added without a new ruling. **Arena = sequential allocation + batch
  free**, where batch free has exactly two variants: whole-arena
  (reset/destroy) and tail-region (mark/rewind — the sidecar-stack
  pattern). **Pool = individual allocation + individual free** (plus
  owner-wide teardown as the safety net). Both share
  single-writer/multi-reader: published allocations are readable from any
  thread; all mutation (alloc, free, reset, rewind, destroy) stays on the
  one writer thread. A site needing non-tail free is reclassified to pool —
  free lists are never added to arena. The general variable-size Pool
  implementation owns growth extents, subdivides them into boundary-tagged
  blocks, and indexes **free blocks by span** in segregated free lists. Its
  optional initial reservation is clamped to at least 1 KiB and rounded up to
  the sequence `1 KiB * 2^n`; each later reservation doubles. Reservations
  below 4 KiB use the context/memtrack block path, and reservations at or
  above 4 KiB use page-backed VM. Each VM commit starts at `max(4 KiB,
  required block bytes)` rounded to the platform page size. A Pool
  user allocation is carved from one of those blocks; it does not call
  `mem_alloc_loc`/`mem_free_loc`, use a pointer hashmap, or maintain an
  allocated-object index. Extent acquisition, release, and context accounting
  remain outside the per-block hot path. [Mem_Heap §1.1–§1.2, §4–§5; MP-12,
  MP-15, MP-21]

- **D4.1.5*** Two mechanism-selection corollaries of D4.1.4. **String
  builders** have two legitimate cases: *standalone* (raw `memtrack`, result
  copied to its destination) and *owner-backed* (buffer allocated from the
  destination pool/arena, so the finished string is handed over with **no
  copy**); pick by whether the destination lifetime is known at build time.
  Owner-backed splits by growth mechanism: **pool-backed grows by `realloc`**
  (buffer may move, no ordering discipline), **arena-backed grows by tail
  extension** (buffer stays put; the string must own the arena tail —
  formatters fit cleanly, parsers must not interleave node allocation),
  falling back to grow-and-abandon or size-then-build when it cannot. An arena-backed builder never `realloc`s. **Interning stores** (NamePool,
  ShapePool) are append-only with no eviction and therefore take an
  **arena**, not a pool; they remain distinct semantic owners and
  `MemContext` nodes. [Mem_Heap §2.3, §4.3; MP-19, MP-20]
### D4.2 Memory Context

- **D4.2.1v3** `MemContext` is a **factory that owns every allocator** — no
  allocator exists off-graph; parent/backing edges wire at creation;
  cascade teardown is in reverse-dependency order. System allocations use
  the hardened `memtrack` substrate: direct allocations use its checked
  `mem_alloc`/`mem_calloc`/`mem_realloc`/`mem_free` contract. Pool is a
  context-owned allocator whose user bytes come from Pool-owned VM extents;
  `memtrack` may account extent metadata and diagnostics but is not the
  ownership mechanism for individual Pool blocks. The platform VM layer is an
  opaque region provider below those owners. Hot allocation paths do not take
  the `MemContext` registry lock; ownership registration and teardown remain
  centralized. [Memory_Context §2–§7; Mem_Heap §5]
- **D4.2.2v2** Per-document sub-contexts key allocators to a document URL,
  with the **attribution/reclamation split**: Radiant documents are
  reclaimable per-document; Lambda `input()` data is GC-coupled and gets
  attribution only. `MemContext` is the single page-allocation failure and
  memory-pressure coordinator. Its VM region layer reserves, commits,
  decommits, and releases page-aligned extents; allocation failure enters
  the context's configured retry/reclamation path before returning `NULL`.
  Legacy `memtrack` pressure callbacks are compatibility adapters and must
  not form a second escalation ladder. [Memory_Context §15]
- **D4.2.3*** Every arena and pool is **bound at creation to a context** — a
  named `MemContext` owner (document/input, parse, eval, validation,
  layout/render, JIT/module, session) recording role, label, parent, and
  thread mode; there are no free-floating allocators. Context teardown
  releases — or drops its reference to (D4.2.4) — every allocator it owns
  and proves its allocator child list empty. [Mem_Heap §1.3; MP-16]
- **D4.2.4*** A shared arena/pool's lifetime is governed by an
  **allocator-level atomic `ref_count`**: created at 1 held by the creating
  context; acquire/release; destroy at zero; the count is the *only*
  cross-thread-mutable allocator field. Batch invalidation — reset, or
  rewind of published data — requires exclusivity (`ref_count == 1`,
  debug-checked). Immortal storage (D4.1.2) is the limiting case: a
  permanent reference. [Mem_Heap §1.4; MP-17]
- **D4.2.5v2*** Allocation tracking is a **diagnostic, never the mechanism**:
  release builds default `memtrack` OFF; STATS is counters-only;
  allocation-level registry records are DEBUG-exclusive — no tracked mode
  may impose a per-allocation global lock on release hot paths, and the
  Pool hot path is registry-free and mutex-free (fixed-header recovery,
  boundary tags, and free-list links owned by the Pool). Pool extent
  reservation/commit and teardown may enter the VM/context slow path, but
  per-block operations do not. Roadmap: the raw surface splits into
  `stack_alloc`/`stack_free`
  (function-scoped LIFO temporaries on a thread-confined sidecar stack)
  plus manager-internal use — free-floating `malloc`/`calloc`/`free`
  disappears from application code, enforced by source audit.
  [Mem_Heap §2.1, §2.4, §5.2; MP-13, MP-14, MP-18, MP-21]

### D4.3 Garbage collection

- **D4.3.1** **Non-moving mark-and-sweep, dual-zone**: objects never move
  (Item pointers stable forever, zero write barriers); only the data zone
  compacts, with exactly one pointer fixup per surviving object — *fixup
  is O(survivors), not O(references)*. Copying GC and handle tables
  rejected. [GC2 §4]
- **D4.3.2v2** Object zone = size-class segregated free lists backed by
  VM-owned extents; data zone = bump allocator fully reset per GC; strings
  keep inline `chars[]` in the object zone. Multiple slabs may share an
  extent, large objects use dedicated regions, and only the owning GC heap
  releases its regions. Adaptive growth threshold (×4 under 40% freed, ×2
  to 74%, hold at ≥75%, capped). [GC2 §5, §12]
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

- **D4.5.1v3** One system-allocation substrate (`memtrack` + VM regions,
  owned through `MemContext`), **two policies**: Lambda traces; Radiant uses
  arenas-as-regions + type-stable pools + generation handles + RAII, never
  GC'd — *the document arena is the cycle collector*. Ordinary Arena owns
  its blocks directly and exposes **batch lifetime only** — whole-arena
  reset/destroy or tail-region mark/rewind (D4.1.4); former individual-free
  users are reclassified, not accommodated. Seam contracts: **pin,
  gen-check, copy-as-value**. [Memory_Model §7; Mem_Heap §4, MP-15]

### D4.6 Name identity

- **D4.6.1v2** One semantic property identity is a `NameId`, never a
  `String*` address. `NameId = [pool16][ordinal16]`; `NAME_ID_NONE` is the
  id-less Input seam, and `SectionNameId = [slot16][offset16]` remains a
  location rather than identity. Generated ordinary names retain catalog
  IDs. The existing NamePool owns resolution; pointer equality is not a
  property, shape, transition, IC, Symbol, or private-name comparison.
  Observable strings may be materialized at Proxy/reflection boundaries.
  [NI1–NI4, NI16]
- **D4.6.2v2** Evolve NamePool, don't replace it (first definer wins and
  parent-first lookup). One identity scope has a sealed static root and one
  owner-thread dynamic child: static segment numbers occupy the lower pool16
  half and dynamic segments the upper half. Static linking uses the existing
  `PropertyKeySpec` sealed image and per-context `NameId[]`; arbitrary dynamic
  IDs are never persisted or emitted as MIR immediates. Mark/Input document
  fields remain id-less unless they resolve through an explicitly retained
  schema/static parent. `eval`/REPL use the same module-table and NamePool
  pipeline; cross-context Input ownership is not inferred from numeric IDs.
  [NI6, NI7, NI10, NI12, NI13]

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

- **D5.2.1v2*** Function wide-scalar returns use the **companion-lane
  convention**: a may-be-wide boxed return is the pair `[item, scalar]` —
  lane 2 a raw 64-bit payload (never a pointer), live iff lane 1 is a
  **pending Item** (reserved tag `0x1E`, kind in the payload). A pending
  Item never lives in memory and at most one is live at a time; the
  caller resolves at first consume (free — the existing tag dispatch),
  patches on escape (one branch), or forwards on tail return (free).
  Typed returns use native lanes with `^E` on a second error-Item lane.
  Classification happens at the wide value's *birth site*, statically —
  never at boundaries. (Reinstates SF14-v1 two-lane returns; supersedes
  v1's caller-donated canonical homes — retired-ABI record in Appendix
  A.) [RV1–RV9, Return_Value §2–§5]
- **D5.2.2v2*** Scalar homes are raw payload words, never GC roots, colored
  in a separate slot space from root slots; fully-unobserved lanes use a
  discard home; tail calls forward the incoming home. Destination-owned
  scalar storage exists at every ownership boundary (array tails, typed
  fields, envs, module tables, async frames, task/message records) **and at
  wide-capable mutable locals**: such a binding owns one slot allocated at
  its declaration and held for its scope, and assigning a wide value copies
  the payload into that slot rather than pushing a fresh home. A binding
  declared inside a loop body is declared once per iteration and its slot
  dies with the iteration; only the mutable form needs the reuse, since an
  immutable binding is written once. (v1 enumerated only the non-local
  boundaries; locals are added because D5.2.3's back-edge reclaim is sound
  exactly when loop-carried wide values sit below the loop-entry watermark,
  which declaration-time allocation guarantees.) [RV16, Return_Value §4a;
  Stack_API §15.3, inv. 20–22]
- **D5.2.3*** **Watermark ownership selects the wide-return transport.** An
  entry that establishes a number-frame watermark tears it down at return,
  so it may not return a pointer into its own extent: Lambda `fn`/`pn`
  therefore use the companion lane (D5.2.1v2). An entry that establishes
  none — a C helper or sys func — allocates in its **caller's** extent, so
  it returns a wide scalar by pushing it on the number stack and handing
  back an ordinary Item, already caller-homed and needing no adoption. The
  rule is stated by ownership, not implementation language, so a sys func
  written in Lambda takes the companion lane and a C helper that ever grew
  a frame would too. Corollary: the per-call watermark snapshot-and-restore
  around helper calls is retired — it is a space bound, not a correctness
  requirement, since the frame epilogue already restores — and the bound is
  re-established by an on-demand reclaim at loop back edges for loops the
  compiler saw make a wide-capable helper call. That reclaim is sound only
  when no loop-carried wide value lives in the reclaimed extent (open:
  `DO24`). Second corollary, sound today and independent of the rest of this
  ruling: the call-effect record already carries a per-callee **watermark
  effect**, and a callee declaring that it leaves the watermark where it
  found it discharges the adopt by the same argument, with no protocol
  change. The emitter writes that effect and never reads it; reading it is
  the cheapest available step. Its enum ordering is load-bearing — the
  conservative value must be zero, so an unaudited row decodes as
  "may allocate". [RV14, RV14a, RV15, RVO11, Return_Value §4a]

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
- **D6.2.2v2** Dynamic calls dispatch through per-callee executable entries.
  LambdaJS function values carry distinct `[[Call]]` and `[[Construct]]`
  capabilities: call dispatch uses `fn->invoke`; construction uses an explicit
  construct entry and passes `newTarget` as an operand, never through a pending
  one-shot side channel. Builtin catalog IDs, names, formal lengths, and class
  labels are metadata and may not select runtime semantics; a declared native
  ABI may select a typed adapter once when the function is created. The
  JavaScript dynamic boundary remains `Item* + argc`; caller-donated scalar
  homes and precise rooted argument spans are ownership-qualified adapters to
  the same entries, not separate dispatch mechanisms. The 16-slot source-
  argument limit remains statically checked where possible with a runtime
  backstop; adapter spans are dynamically sized, precisely rooted, and
  LIFO-destroyed. Dynamic calls with named arguments and dynamic calls to
  `var`/inout signatures remain rejected. [Function_Arg, LC call-ABI; JC1–JC4]
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

- **D7.4.1v2** `Item` is the only value currency; native structs cross as
  **VMap projections**, and engine-owned internal-slot objects may use the
  same carrier when native slots are primary. VMap is selected by
  representation, not module ownership; shape-backed ECMAScript objects
  remain typed Maps. Brand, precise trace, and finalize are vtable-owned; raw
  C pointers never become script-visible, and system resources are integer
  **rids**.* [JA6, NM §6.3, §8; JR7]
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
  G1 gate). The JS merged Item error lane (D8.4.3) is not reused as another
  guest's exception model.* [Lang_Hosting §5–§7, §13, D8.4.3]
- **D7.4.4** **Declared interfaces + record-owned hooks are the ONLY
  host-object protocol — one way to be a host object, no fallback tier.**
  A host type's surface is its Lambda-type-syntax interface declaration
  compiled to member records, plus the record-owned open-name/indexed
  hooks (`named_get`/`named_set`/`object_*`, `indexed_get`) and
  `JubeTypeDef.destroy` for lifecycle. The type-level dispatch fallbacks
  (`JubeTypeDef.host_ops`, `JubeTypeBinding.legacy_ops`) are **retired**:
  no instances, consumer paths deleted, struct surface removed with a
  `JUBE_ABI_VERSION` bump. Corollary: property keys cross the member ABI
  as `Item`s (string | symbol | number), are resolved internally via one
  borrowed byte view, and are never re-materialized for a fallback
  consumer. [DOM4 D4k, D4j]
- **D7.4.5*** **Three virtual carriers mirror the container taxonomy:
  `vmap`, `varray`, `velmt`.** Host data crosses as the virtual carrier
  matching its shape — virtual map (exists), virtual array (indexed face +
  length via vtable, `type()` = "array"; live by reading, not materialized
  copies kept fresh by mutation sweeps), virtual element (Lambda
  `Element`'s dual list+map nature: children list face, named map face,
  tag). Refines D7.4.1's "VMap projections": VMap was never the shape
  ruling, only the first carrier; representation still selects the
  carrier. Declared interfaces / member records (D7.4.4) are the
  named-member protocol on ALL carriers; the carrier decides which
  structural faces exist virtually. Flattening element-shaped data into a
  map projection, or materializing array-shaped host state into real
  arrays with liveness-by-invalidation, are both anti-patterns this
  ruling retires. [DOM4 D4l, D7.4.1, D7.4.4]

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

- **D8.1.1v2*** Tree-sitter grammar → typed AST → **tiered execution**:
  a boxed AST-walking interpreter (**T0**) is the default execution mode
  and the retained AST is the runtime source of truth; **MIR Direct**
  (`transpile-mir.cpp`) → MIR JIT (**T1**) compiles individual hot
  definitions on demand (per-definition promotion cells, default 3rd
  call), with whole-module eager compilation under explicit policy. The
  const-folder is the same engine in CONST mode; MIR-interp demotes to a
  codegen diagnostic. Promotion swaps entry-pointer data, never patches
  code (D8.4.1, DI14); D5.1.2's "no hotness detection" continues to
  scope stack primitives — definition-site counters are tier policy, not
  primitive adaptivity. *(Historical: v1 ruled the opposite — "no
  AST-walking interpreter", MIR-interp as sole non-JIT path [U26] —
  reversed by user ruling 2026-08-15; the full record is
  `Lambda_Design_Unified_AST.md` §12.)* [AI1–AI22]
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
- **D8.2.4*** The unified compiler has one **indexed compilation unit**:
  dense stable IDs name nodes/scopes/bindings/functions/classes; one common
  child-enumeration contract builds parent, owner, binding, use/def and graph
  indexes; lowering consumes resolved identities and never repairs binding.
  Core passes may not grow private core-child walks. [U27, U28]
- **D8.2.5*** One typed pass manager owns build → bind → validate → index →
  capture/effect → type/representation inference → function planning → MIR
  lowering/finalization/link. Passes declare required/produced facts;
  source contracts remain on the AST and erasable optimization facts remain
  in ID-keyed side tables (D2.4.1, D3.2.3, D3.3.1). Profiles answer typed
  semantic questions and extension nodes; they do not own alternate pass
  schedules. [U29, U30]
- **D8.2.6*** Core expression lowering is demand-driven and returns the full
  `MirValue` of D2.4.2. `DISCARD`, `ANY`, `REQUIRED_REP`, `DEST_REG`, and
  `BRANCH` may avoid materialization but never change semantics; unsupported
  demand falls back to generic lowering. Carrier conversion remains
  `em_require_rep()` (D2.4.3), semantic coercion remains profile-owned, and
  root/final-store ownership remains emitter-only (D5.3.4). [U31, U32]

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

- **D8.4.1v2** **No inline caches anywhere in the Lambda lane or LambdaJS** —
  *shared semantic dispatch over per-site caching*: specialized lowerings have
  no dynamic-dispatch sites by construction; open sites use runtime dispatch;
  a hot open site is a DF12 inference bug first. Multi-version dispatch, when
  it comes, is a guard chain, never a patchable cache. LambdaJS named and
  indexed property accesses use the shared runtime reference/property kernels;
  TypeMap shape metadata remains ordinary lookup data, not mutable per-site
  cache state. Generated code stays immutable. [LC1]
- **D8.4.2v2*** Core direct calls pass individual ABI operands (`Context*`,
  args); returns are shaped per the companion-lane convention (D5.2.1v2)
  — the trailing scalar-home operand is retired. `Item* + argc` is the JS
  dynamic boundary only. [LC call-ABI, RV1, RV10]
- **D8.4.3** **Fallible JS/Jube helpers use the merged Item error ABI.** A
  helper that can raise returns an ERROR-tagged `Item` and never relies on a
  pending flag or a separate poll result; MIR lowering tests the returned
  tag, and try/catch/finally routing carries that same Item identity. Raw
  scalar helpers are permitted only when their catalog contract is
  infallible (`PRESERVES`). `LambdaError` is the shared ERROR carrier; its
  Map-compatible resting prologue is traced as a heap reference, and JS
  Error stack materialization may remain lazy. [S7.4.4, D1.4v2, JR3]

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
- **D8.6.4v2*** Unified-AST consolidation has three **hard, fail-closed exit
  ratchets**: at least 2,000 net physical C/C++ lines removed from the
  anchored `lambda/runtime` + `lambda/js` scope; at least 10% lower internal
  parse-through-link compiler time for the complete `test_lambda_gtest`
  corpus; and at least 20% lower for `test_js_gtest`. Finalized MIR volume for
  the frozen `test_js_gtest` large-library cohort and complete JS corpus is a
  required numeric diagnostic, not an exit ratchet: it is counted from
  finalized top-level module functions with the same instruction definition
  as MT7 (labels/declarations are excluded), printed as a machine-readable
  per-test record, compared on identical manifests, and any material growth is
  attributed. Timings use identical release-mode sample manifests and the
  median of five complete measured runs after one warm-up; execution, process,
  cleanup and scheduler time do not count. Cache hits, missing/retried samples,
  code moves out of the LOC scope, formatting, and comment stripping cannot
  satisfy a ratchet. [U33–U36]

---

## Appendix A — Implementation Footnotes

Status of `*`-marked rulings as of 2026-08-15.

| Ruling | Status |
|---|---|
| D2.1.6 | Guardrail layer partial: ~24 raw `>> 56` sites across 11 files, open-coded `get_double` derefs, raw `MIR_EQ` emissions outstanding. |
| D2.3.2 | Container unbox helpers + `p2it` returns designed, not landed (Box_Unbox2 Phase 1); MIR path still boxes container params as ANY (safe, unoptimized). |
| D2.4.1–D2.4.3 | `MirValue`/`em_require_rep` infrastructure exists; LambdaJS uses it; propagation through Lambda expression lowering not started (phases L0–L6), sequenced after nullable-lane work. |
| D2.5.1 | Nullable-lane first slice landed 2026-08-05 (LaneStorageDesc, native arrays, packed nullable fields, scalar ABI); `f16?`/`f32?`, JS IC lowering, mutable ArrayNum views, vector/N-D kernels pending. |
| D2.6.2 | ArrayNum `==` representation-sensitivity is a known live bug (also gates the data-processing engines). |
| D2.6.3 | ELEM_INT i64 revert landed; SIMD kernels only partly re-enabled (C16-era gating comments remain). |
| D2.7.2v2 | v2 (companion-lane entries) decided 2026-08-14, not implemented — the v1 trailing-home wrapper ABI ships until Return_Value P4. Ownerless-slot GC scalar fallback active and counted; removal gated on the per-boundary inventory reaching zero. SG2 OQ audits open (dispatch-helper enumeration, resume-path slot reads, RetItem census). |
| D2.8.2–D2.8.3 | TE-17 lane gating is designed, not built: the admission predicates exist (`lambda_type_accepts_error`, `lambda_type_lane_storage_desc`) but no lane-entry decision consults them, and the `may_defect` fixed point they need does not exist (D6.1.3) — so the current polarity is "trusted clean", the wrong direction. Known violation V1: `fn_array_set` silently despecializes a declared `int[]`, which keeps D2.8.1 true (the lane is lost, not poisoned) but makes the S7.7.2 dominance guarantee false today. |
| D3.1.1 | `Type*` kind-discrimination is code-authoritative only — no design record owns the first-class type-value representation (DO22); the type-graph de-pointering census is deferred to its own doc (CP §6 census C). |
| D3.2.2 | Constrained-type enforcement is base-only; the `is`/`fn_is`/validator three-way divergence is open (TE-6 P5). |
| D3.4.3 | Shape pool shipped for `Input` (contrary to its doc's stale "planning" header); the runtime/EvalContext shape pool (Shape_Pool Phase 5) is not implemented — runtime maps rebuild per transition instead of interning. |
| D4.1.4v4 | Semantics remain live; the separate arena-level `arena_mark`/`arena_rewind` promotion is still pending (ScratchArena currently provides `scratch_mark`/`scratch_restore`). Pool v2 core landed in Mem_Heap R7 on 2026-08-11: `pool_alloc` carves user bytes from Pool-owned growth extents, uses boundary-tagged blocks and segregated free lists, and no longer calls `mem_alloc_loc`/`mem_free_loc` per user allocation or uses a pointer index. Growth starts at an optional reservation clamped to 1 KiB and rounded up to `1 KiB * 2^n`, doubles subsequent reservations, uses the context/memtrack path below 4 KiB and page-backed VM at/above 4 KiB, and commits at least `max(4 KiB, required block bytes)` page-rounded. |
| D4.2.1v3 | MemContext owns allocator identity/lifecycle; hardened memtrack and the VM region provider are the normative system-allocation substrate. Pool is a context-owned VM-extent allocator; its user blocks are not `memtrack_pool_*` allocations. The Pool v2 core migration is landed and verified; the broader every-allocator context-binding audit remains pending. |
| D4.2.2v2 | Stage 2 page allocation and the single MemContext failure coordinator are implemented as the allocator-retirement foundation. Full cost-based reclaimers remain follow-up work. |
| D4.1.5 | String-builder two-case model matches the code (`StrBuf` standalone; `StringBuf` owner-backed, pool-backed `realloc` growth). The arena-backed tail-growth variant does not exist yet — it needs the `arena_mark`/`arena_rewind` promotion of D4.1.4 (Mem_Heap R6). Formatters are the first consumer (already structurally ready: destination pool passed in, scratch on a separate pool, no destination allocations during the walk); parsers follow, subject to the tail-ownership hazard. NamePool/ShapePool still take a `Pool*` — conversion is Mem_Heap R4b. |
| D4.2.3 | MemContext graph exists; the every-arena/pool context-binding audit (Mem_Heap R6) has not run. |
| D4.2.4 | Ref-counting not implemented; shared-allocator census (Mem_Heap §15 Q7) pending. |
| D4.2.5v2 | R1a landed 2026-08-10 (release default `MEMTRACK_MODE_OFF`, measured: primes2 825→155 ms). R3a removed the historical Pool side index, and R7 landed the Pool-owned boundary-tag/free-list hot path with no global mutex or registry call on block operations. STATS counters-only (R2), the `stack_alloc` split (MP-18), and independent release-performance evidence remain pending. |
| D4.3.2v2 | GC size classes and data-zone policy are retained; backing storage is owned by MemVmRegion and released by the owning GC heap. |
| D4.5.1v3 | Radiant and Lambda keep distinct policies over memtrack/VM ownership; legacy Pool/Arena backend wording is superseded; v3 records batch-only arena lifetime (two variants, D4.1.4). |
| D4.4.3 | COW Stage 1 landed 2026-07-23; Stage 2 (exclusivity faces, view confinement, module-`var` rule, snapshot iteration) deferred, designed. |
| D4.6 | Name identity is a PROPOSAL (rev 5): W1/W2 integer schemes can start now; W4 stage 3 blocked on the MIR-cache reconciliation (NI §8). |
| D4.7 | Const pool / MarkPack is a DRAFT (rev 4): baked-pointer census verified against emitters 2026-07-31; phases CP-P0..P4 not started. |
| D5.2.2v2 | v1 clauses ship; the local-binding clause added 2026-08-14 is not implemented. There is no per-source-binding scalar storage today: `MirScalarHomeBinding` associates a MIR *register* with a colored home — transient-value coloring, not binding ownership — and the `BindingStorage` classification (`REGISTER`/`SCOPE_ENV`/`MODULE`/`PERSISTENT`) has no consumer in the emitter at all. So a wide-valued mutable local is currently a register holding an Item that points at whatever home its producing expression happened to receive. Building the clause is new machinery, but it subsumes rather than adds: per-binding slots plus D5.2.3's bulk back-edge reclaim replace the interference/coloring pass in `em_finalize_scalar_homes`. Sequenced with Return_Value P2.7, which depends on it. |
| D5.2.3 | Decided 2026-08-14, implementation pending (Return_Value P2.7). The companion-lane half is what P0–P1.3 landed; the C-helper half is untouched — helper returns still pay the v1 ritual (per-call watermark snapshot, a 20-instruction adopt cluster, a colored home), measured at **68% of all adopt sites** across AWFY (757 of 1108), the largest single population in the census. Retiring it is blocked only on `DO24`: the back-edge reclaim that re-establishes the space bound must first be shown safe for loop-carried wide values. The bound matters — without a reclaim, an untyped million-iteration loop over `int64` storage grows the side stack by 8 bytes per iteration, so the gate for this phase measures peak side-stack usage, not just correctness. |
| D5.2.1v2 | Decided 2026-08-14; the v3 companion-lane convention is the shipping default (Return_Value P0–P3), including context-slot public wrappers. The **v2 trailing-scalar-home ABI remains compiled only as a compatibility fallback** until P2.5/P2.7/P5 close. Record of the retired default design: every function carried a hidden trailing home address (a liveness-colored number-stack slot, `_scalar_home`); the callee classified its boxed result inline — a 20-instruction adopt cluster per site (`em_adopt_scalar_item_value`) — and copied wide payloads into the donated home; unobserved lanes used a discard home and tail calls forwarded the incoming home (the D5.2.2 discard/forward clauses lapse with the ABI at P5; D5.2.2's destination-owned scalar storage is unaffected). Retirement evidence (2026-08-14 MIR census): the ritual was 9–39% of emitted MIR across AWFY (~11–16 executed instructions per boxed return), paid signature-blind while wide scalars almost never occurred. |
| D6.1.3 | `may_await` analysis exists; the `may_defect` split does not — `may_return_error` is overloaded and the missing-analysis polarity is currently "trusted clean" (wrong direction; one half of the measured O1 divergence). |
| D6.3.2 | Worker tier pending entirely: process isolation first, thread isolation gated on the isolate-state audit and DO20. |
| D7.1.3 | Static modules implemented (rev 29, P0–P6) except Class F: the rt→radiant boundary is a ratcheted 165-import baseline; P1c constructor consolidation deferred. |
| D7.4.1v2 | Native-module POC 1 remains unstarted; the engine-owned Promise VMap is designed by JR7/Tune7 but not yet implemented. |
| D7.4.3 | Hosted-language layering: `lang-python` is the landed DSO reference chain, but Python is currently statically linked and its ten follow-up ADRs (Lang_Hosting §17) are unwritten. |
| D7.4.4 | Implemented in DOM4 (2026-08-14): `host_ops`, `legacy_ops`, `JubeHostObjectOps`, and the vmap `string_key_item` re-materialization shim were removed; record-owned hooks are the only host-object protocol and the ABI is version 4. |
| D7.4.5 | Direction only; implementation deferred past DOM4 (user ruling 2026-08-13: DOM4 settles vmap first; the runtime is likely not ready for new carriers). `varray` and `velmt` do not exist: DOM collections are materialized Arrays with companion-map decoration and a 4096-entry issued-collection cache refreshed per mutation (js_dom.cpp); Radiant `Velmt` handles are struct-copied into VMap payloads with strcmp projection. varray + collection conversion = future Jube stage; DOM-node carrier move to velmt = DOM4 OQ9 (DOM5-scale). |
| D7.5.1 | T1 verification layers staged; T2/T3 directional, neither built (not required until a third-party module story). |
| D7.5.2 | Central IO API direction adopted; surface not extracted (`js_fs`/`js_os`/`js_net` raw-IO violations are the burn-down list); `dynamic_lookup` laxity is acknowledged debt. |
| D8.1.1v2 | Decided 2026-08-15 (user ruling); implementation not started — the shipped pipeline remains whole-module eager MIR JIT with MIR-interp as the size-pressure valve until Ast_Interpreter phases P0–P5 land (`vibe/Lambda_Design_Ast_Interpreter.md` §11). |
| D8.2.1–D8.2.3 | Structural Unified AST convergence is substantially landed for Lambda/JS: common core catalog, node aliases, `FnAnalysis`, and `MirEmitter`; Python remains the guest acceptance test. |
| D8.2.4–D8.2.6 | Indexed compilation unit, authoritative traversal, typed fact/pass process, and demand-driven full-contract `MirValue` continuation are designed in U27–U32; implementation not started. |
| D8.3.4 | DF16 guard hoisting decided, flag-gated, unimplemented (P7); DF12 speculative lifting deferred (P5); §10 multi-version specialization future; the size-gate threshold unset. Dual-func Stage 1 core (P0–P4, P6) complete. |
| D8.4.2v2 | Decided 2026-08-14 with D5.2.1v2; v3 transport is the shipping Lambda call ABI. The trailing scalar-home operand remains compiled only for v2 compatibility and LambdaJS until Return_Value P2.5/P5. |
| D8.4.3 | Landed 2026-08-07 with JS Tune1: JS/Jube fallible helpers use one merged Item error lane; pending-exception polling and the legacy flag are deleted. |
| D8.5.1 | MIR cache L1 landed; L2 lazy codegen approved but `mir.c` still eager. |
| D8.5.2–D8.5.3 | L3 code-image cache: nothing landed (D0–D6 sequence); de-pointering (MC4) independently shippable, not started. |
| D8.6.4v2 | Timing/MIR instrumentation is landed. The LOC and compiler-time ratchets remain open against anchor `e66e5b5c71bc7ee7fe2d1e2b2a9afe27dc6825a3` at 319,606 lines; large-library and complete-corpus MIR counts remain required diagnostics, not exit gates. |

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
- **DO15** *(resolved 2026-08-07)* Online exception-poll doc status
  conflict: verified landed in `52c0f3c02` (2026-07-24) — `exc_track` is the
  live mechanism, the G1 peephole and `jm_optimize_exception_polls` pass are
  deleted from the tree; the impl doc's status line now reads IMPLEMENTED.
  OE1–OE10 may be cited as landed. History: `vibe/jube/JS_Runtime_Redesign.md`
  JR3.
- **DO25** Interpreter tier (D8.1.1v2) opens: satellite-module treatment
  under the MT7 emission budgets (AIO2); cross-context visibility of
  promotion cells (AIO8); once-called hot bodies — `run`-mode `main`
  with heavy inline loops never re-enters, so backedge marking never
  pays; escape hatches vs eventual OSR (AIO11); T0 TCO parity before the
  default flip (AIO1). [Ast_Interpreter AIO1–AIO12]

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
- **DO24** Unnamed wide temporaries crossing a loop back edge, under
  D5.2.3's reclaim. **D5.2.2v2 settles the named case**: a wide-capable
  mutable local owns a slot allocated at its declaration, which precedes the
  loop and therefore sits below the loop-entry watermark, so the reclaim
  cannot reach it while per-iteration transients above it are freed. What
  remains open is whether a *compiler-generated* wide temporary — not a
  source binding, so it receives no slot — can be live across a back edge.
  That set ought to be empty once bindings own their storage, but the
  reclaim must be gated on the emitter positively establishing it, not on
  the assumption: a wrong answer is a use-after-free, not a slowdown. The
  question is now bounded and per-loop rather than open-ended.
  [RVO11, Return_Value §4a]

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
| D2.8 | TE-15/TE-17/TE-18; IEH I1–I4 | `Lambda_Design_Type_Enforcement.md`, `Lambda_Impl_Error_Handling (done).md` |
| D3.1–D3.3 | C8.5-4, C9a; TE-1/TE-6/TE-10/TE-13; DF12/DF13; B7; Lane §1 | `Lambda_Semantics_Formal2.md`, `Lambda_Design_Type_Enforcement.md`, `Lambda_Design_Compiling_Dual_Func.md` |
| D3.4 | Shape_Pool §1–§8; Transpile_Map DD1–DD4; NI10/NI13; Nullable §6; TE §6 B7b | `Lambda_Shape_Pool.md`, `Lambda_Transpile_Map.md`, `Lambda_Design_Name_Identity.md` |
| D4.1 | GC1 §2.10.4; CW8; SF16; CR8; Mem_Heap §1 (MP-12, MP-15) | `Lambda_Garbage_Collector.md`, `Lambda_Design_Runtime_COW.md`, `Lambda_Design_Stack_Rooting.md`, `Lambda_Design_Mem_Heap.md` |
| D4.2 | Memory_Context stages; Mem_Heap §1.3–§1.4, §2, §9 (MP-13, MP-14, MP-16–MP-18) | `vibe/Memory_Context.md`, `Lambda_Design_Mem_Heap.md` |
| D4.3 | GC2 §4–§12 | `Lambda_Garbage_Collector2.md` |
| D4.4 | CW1–CW21 | `Lambda_Design_Runtime_COW.md` |
| D4.5 | Memory_Model §5–§7 | `Lambda_Design_Memory_Model.md` |
| D4.6 | NI1–NI16, W1–W6 | `Lambda_Design_Name_Identity.md` |
| D4.7 | CP1–CP26 | `Lambda_Design_Const_Pool.md` |
| D5.1–D5.3 | SF1–SF20, OS1–OS11; Stack_API phases + invariants; CR1–CR8, RH1–RH8; Merges A/B/C | `Lambda_Design_Stack_Frame.md`, `Lambda_Design_Stack_API.md`, `Lambda_Design_Stack_Rooting.md` |
| D5.2, D2.7.2, D8.4.2 | RV1–RV16 (+ RV3a, RV10a, RV14a), RVO1–RVO11 | `Lambda_Design_Compiling_Return_Value.md` |
| D5.4 | RG0–RG14, MT2 contract | `Lambda_Design_Runtime_Globals.md` |
| D6.1 | U14, U26; Features §3.6; NM §6.2; Lang_Hosting §7.1; IEH §5.3 | `Lambda_Semantics_Features.md`, `Lambda_Design_Native_Module.md`, `Lambda_Impl_Error_Handling.md` |
| D6.2 | C8.7; Function_Arg; DF7/DF11; SF18; JC1–JC12 | `Lambda_Semantics_Formal2.md`, `Lambda_Design_Function_Arg.md`, `vibe/jube/JS_Runtime_Callable.md` |
| D6.3 | K11–K32 (runtime side); ER-D1/D11 | `Lambda_Design_Concurrency.md`, `Lambda_Design_Exec_Recovery.md` |
| D6.4 | Sys_Func §7–§8 | `Lambda_Design_Sys_Func.md` |
| D7.1 | SM1–SM14 | `Lambda_Design_Static_Modules.md` |
| D7.2 | RG14; DF15; ER-D2; MC1 | `Lambda_Design_Runtime_Globals.md`, `Lambda_Design_Compiling_Dual_Func.md`, `Lambda_Design_Exec_Recovery.md` |
| D7.3–D7.5 | JA1–JA16; Native_Module §6–§10; Lang_Hosting P/C + §5–§13 | `Lambda_Design_Jube_Architecture.md`, `Lambda_Design_Native_Module.md`, `Lambda_Design_Jube_Lang_Hosting.md` |
| D8.1–D8.2 | U1–U36; AI1–AI22, AIO1–AIO12 | `Lambda_Design_Unified_AST.md`, `Lambda_Impl_Tune_Ast.md`, `Lambda_Design_Ast_Interpreter.md` |
| D8.3 | DF1–DF17, O1–O14 | `Lambda_Design_Compiling_Dual_Func.md` |
| D8.4 | LC1 + call-ABI notes | `Lambda_Design_Compiling.md` |
| D8.5 | MC1–MC8; L3-1–L3-10 | `Lambda_Design_MIR_Cache.md`, `Lambda_Design_MIR_Cache_L3.md` |
| D8.6 | MT1–MT8; U33–U36 | `Lambda_Design_MIR_Emission_Test.md`, `Lambda_Impl_Tune_Ast.md` |

The decision records preserve the full deliberations — every alternative
that lost and the arguments that did not persuade. This specification is
their distillation: the record governs the history; this document governs
the design.

# Lambda Formal Design — Specification

**Spec version:** 67.0.0 (2026-11-11)

**Status:** normative. This document is the single source of truth for the
design decisions that implement
[`Lambda_Formal_Semantics.md`](Lambda_Formal_Semantics.md). If another
document or the implementation disagrees with it, this document wins. The
`vibe/` design records keep the history, the rejected alternatives and the
detailed design.

**Scope:** the Lambda core runtime, LambdaJS where it shares that runtime,
Jube module hosting, the build profiles, and the shipped document packages.
Radiant layout and rendering have their own documents. The cross-language
runtime contracts in D1.4 and D7.4.3 apply to every language that runs on the
Lambda runtime.

**Principles first.** This document records design principles and the
decisions that follow from them. Principles stay; details change as the
implementation moves. Thus a ruling states its principle in its title and its
decision at a stable level. It does not record tuning values, internal names
that can change, measurements or implementation history. Those items belong
in the vibe design records and in the
[implementation history](../vibe/impl/Lambda_Impl_Formal_Design_History.md).
A change of implementation detail must not need a change to this document.

**Style.** This document uses ASD-STE100 Simplified Technical English: short
sentences, active voice, present tense, and one statement in each sentence.
Code names are technical names.

**Ruling IDs.** A ruling ID is a section path: `D2.3.1` is the first ruling
of §2.3. A revised ruling keeps its ID and gets a version suffix (`D2.3.1v2`),
and the document version changes by semver. The bare `D` series belongs only
to this document. `DI#` are invariants. `DO#` are open design issues
(Appendix B).

**Renumbering (61.0.0).** Version 61.0.0 restructured this document and gave
new IDs to all rulings. All version suffixes started again. The same change
migrated every citation in the repository. Git history, commit messages and
branches from before 2026-11-04 still use the old IDs. The
[implementation history §1](../vibe/impl/Lambda_Impl_Formal_Design_History.md#1-old-to-new-ruling-ids)
maps each old ID to its new ID. To find the new ruling for an old ID, run
`python3 utils/formal_index.py --old D8.1.1v14`.

**Implementation marks.** A `*` after an ID means that the ruling is not
implemented, or is only partly implemented. Appendix A gives the status.
Appendix C maps each section to its decision records.

**Contents.** D1 Architecture principles · D2 Value representation · D3
Containers and shapes · D4 Types · D5 Memory · D6 Execution state · D7
Functions and calls · D8 Compilation and execution tiers · D9 Build, modules
and packages · D10 Documents and shipped packages · D11 Security and
capabilities.

---

## D1 Architecture Principles

These principles apply to all parts of the runtime. The rulings in D2–D11
apply them.

- **D1.1 — One host, many bundles.** There is one `lambda.exe`. Bundles
  differ only in the Jube modules that they ship beside it. The host binary is
  the same in all bundles. A guest language is a dialect on the Lambda
  substrate: a grammar, an AST builder and a `LangProfile`. Lambda does not
  claim full compatibility with a guest language. What the profile does not
  cover is not supported. [JA4, J5, U1]
- **D1.2* — `Item` is the only value currency at a host boundary.** Lambda,
  Jube, native modules and every host boundary exchange values only as
  `Item`. A guest execution ABI with its own version can keep private values
  while it runs. A private value never crosses a host boundary, and it is
  never read as an `Item`. A crossing needs an explicit adapter that owns the
  conversion. Native host structs cross as virtual carriers (D9.5.5). System
  resources cross as integer resource IDs. [JA6, JM22]
- **D1.3* — Guests share the host substrate.** A host context has one MIR
  library and not more than one event loop. A guest first uses the shared
  Lambda components: source admission, parser and AST facts, MIR lifecycle,
  diagnostics, memory accounting, logging, test gates, and the
  return-and-cleanup discipline. A guest can own a private value heap, root
  stack or semantic kernel only where its private ABI (D1.2) makes the shared
  component incorrect. Each such boundary must have a name and a test. A guest
  never gets a second host runtime, event loop, allocator framework or Lambda
  stack owner. [Lang_Hosting P1–P10, C1–C10, JM22]
- **D1.4* — A language failure returns through every frame.** A function that
  can fail returns an explicit completion. The Lambda host ABI uses an `Item`
  success-or-error carrier, or a proven native lane with a companion error
  lane (D6.2.1). A private guest ABI uses its own completion until an adapter
  returns to the host. An error, a throw or a rejection moves to the caller
  only by a normal return. Each caller examines the failure before it uses a
  success value, and it runs its own cleanup. No pending-exception flag, C++
  exception, `setjmp`/`longjmp`, SEH unwind, signal jump or guest unwind
  carries a language failure. The only exception is a native system fault
  from the closed set of S7.11.1. Such a fault can abandon frames through a
  recovery frame (D6.3.6). That path is never an error path or a guest
  exception path. [S7.4.3, S7.4.4, S7.11.1–S7.11.4, REH-D1–REH-D5, JA5, J3,
  JM22]
- **D1.5* — Precise GC everywhere, permanently.** Conservative scans of the
  native stack are removed from all builds. They must not come back. A guest
  is precise only if all these conditions are true:
  - a root interface (shared, or private under D1.2) gives every live guest
    reference;
  - its collector traces all guest edges;
  - forced-collection tests prove the contract. [CR1–CR8, JM22]
- **D1.6 — MIR Direct is the only compiler back end.** The C-text C2MIR back
  end and its CLI flag are removed. Do not add a second back end that
  generates C text. [rule 14; SM14, CR7, U11]
- **D1.7 — Source is the distribution format.** Scripts are distributed as
  source. MIR is a private in-memory IR, not a distribution format. A compiled
  artifact is a local cache. Its key is the build ID and the content hash.
  [JA13, MC7/P14]
- **D1.8 — Carry decisions forward; never recover them.** The compiler carries
  semantic contracts and representation choices forward explicitly. It never
  reconstructs them from emitted MIR register classes. No value that depends
  on the context lives at an address that is fixed in the code. [Lane §1, MT2]
- **D1.9 — Fail closed.** An unresolved call is `MAY_GC` and has unknown
  effects. An unsupported representation conversion stops the program in
  every build. Malformed input is an error, never undefined behavior. A log
  setting must not make malformed MIR acceptable. [Stack_API §7.2, Lane §4.3,
  CP18, MT2]
- **D1.10 — Enforcement is part of the design.** Each invariant has a static
  assert, a debug check, a lint rule, a ratchet or a CI gate. The tag budget
  has an owner. The emission size has a ratchet with 0% slack. Module
  boundaries have link checks. Each module kind names its conformance gate,
  and no module lands without its gate. [Item_Boxing R7, MT7, SM5, JA9]

### The invariant ledger (DI)

These invariants come from the rulings. Each one holds in every build, unless
it says otherwise. A violation is a defect. The semantics spec has the
language-level ledger (SI).

- **DI1 — The Item contract.** `sizeof(Item) == 8`, and an `Item` is passed
  by value. Leaf pointers fit in the low 56 bits. Raw container pointers stay
  in the supported low address range, outside the inline-double space.
  AArch64 top-byte metadata never goes into an `Item`. [D2.1]
- **DI2 — Canonical encoding.** Each value has one encoding in each build.
  `get_type_id` is the only interface for the semantic type. A compare of raw
  Item words is not semantic equality, unless canonical encoding proves it.
  [D2.1.2, D2.1.4]
- **DI3 — The tag budget has an owner.** Inline doubles leave 64 legal
  high-byte values. A change that claims one must also update the partition
  assertions and this document. [D2.1.3]
- **DI4 — Lane sentinels are private.** An int-lane sentinel never goes into
  an `Item`, a container or a guest bridge. An INT-tagged `Item` always
  decodes to a finite value. [D2.2.2]
- **DI5 — No sentinel laundering.** Code tests a nullable lane value before it
  uses the value in arithmetic, a compare, a dereference or a typed store. A
  plain `T` carrier never holds a null sentinel. `box(NULL_LANE(T)) ==
  ItemNull` exactly. [D2.4]
- **DI6 — The GC sees only pointers.** Numeric and boolean sentinels and
  scalar homes are never roots. The collector never scans the number stack.
  [D2.4.1, D6.1.1, D6.2.2]
- **DI7 — No scalar cells in the GC heap.** An `INT64`, `UINT64` or `FLOAT`
  payload is never a separate GC allocation. An Item with one of those tags
  never points into the GC heap. [D2.6.1]
- **DI8 — ArrayNum is null-free.** Its contract can be `T?`. Its storage never
  holds a null encoding. An admitted null store first demotes the array.
  [D3.2.2]
- **DI9 — Containers own their scalars.** A wide-scalar Item in a container
  points only into the buffer of that container. A header never moves away
  from its identity. A wide optional slot is a destination-owned `TypedItem`.
  [D2.6.2]
- **DI10 — The safepoint contract.** A collection starts only inside a
  `MAY_GC` call. `MAY_GC` is the default. `NO_GC` is a claim that a tool
  verifies. All `MAY_GC` cleanup runs before the watermarks are restored.
  [D6.3.2, D6.1.4]
- **DI11 — One context, no fixed addresses.** There is one `EvalContext` per
  isolate and one TLS root. No value that depends on the context is at an
  address that is fixed in the code. Capsules never move. [D6.4]
- **DI12 — Hot paths have no locks.** Do not add a lock, an atomic
  read-modify-write, a publication check or a coherence operation to a path
  that executes repeatedly. [D6.4.4]
- **DI13 — Representation is carried.** Semantics, representation,
  nullability and ownership never come from emitted MIR register classes.
  [D2.3.4]
- **DI14 — Entry integrity.** Only two paths give the proof to enter an
  unboxed entry. An input that fails the guard never reaches it. Generated
  code is immutable; it is never patched. [D7.3.3, D7.4.1]
- **DI15 — No language failure skips a frame.** Errors and guest exceptions
  move between activations only as an explicit completion and a normal
  return. Each generated or native frame examines the failure and runs its
  own epilogue. The recovery frame is only for native system faults. A fault
  never crosses a thread. [D1.4, D6.1.4, D7.4.3, D7.5.3]
- **DI16 — Initialization is a transaction.** A package or module that is only
  partly initialized is never visible. Initialization commits or rolls back.
  [D9.3.2, D9.4.2]
- **DI17 — One value currency.** `Item` crosses every boundary. A raw C
  pointer never appears in a signature that a script can see. A compiled
  artifact is a local cache; it is never distributed. [D9.5.1, D1.7]
- **DI18 — The emission ratchet.** Emission budgets have 0% slack. The commit
  that makes emission larger also changes the budget. [D8.6.1]
- **DI19 — The COW bit.** The shared flag changes in one direction only. A
  false "shared" costs a copy. A false "unique" is a semantic bug. The fast
  path for a unique value is one byte load, one test and one branch. [D5.4]
- **DI20 — Native lanes are error-free.** An `error` value never occupies a
  native carrier: array storage, a packed field, a native local, or an unboxed
  parameter or return. No unboxed operator receives an error operand. A value
  that is not proven free of errors stays boxed. The gate is static, and it
  fails closed. [D2.5]

## D2 Value Representation

*A value has one meaning and can have several representations.
Representation never changes meaning (S1.6).*

### D2.1 The Item model

- **D2.1.1 — An Item is one word with three storage classes.** An `Item` is
  one 64-bit word, passed by value. It has exactly three storage classes:
  - an inline tagged immediate: bits 63–56 hold the TypeId, and bits 55–0
    hold the payload;
  - a tagged scalar-leaf pointer: the same split, with the pointer in the low
    56 bits;
  - a raw header pointer for a container: the whole word is the pointer, and
    the TypeId is at byte 0 of the object.

  Scalars are type-first; containers are data-first. Strings, symbols and
  binaries stay tagged leaf pointers. Representation uniformity is not a goal.
  [Item_Boxing §0, §3.1, §4]
- **D2.1.2 — One interface gives the semantic type.** `get_type_id(Item)` is
  the only approved interface for the semantic type. Its inline-double test
  comes first and does not change the container branch. Semantic equality has
  one entry point. A compare of raw Item words is not semantic equality,
  unless canonical encoding proves it. [Item_Boxing §3.2, R9]
- **D2.1.3 — Extend containers by layers, not by tags.** Container kinds
  extend in layers: storage class, then the `Container.type_id` family, then
  kind and flags, then shape, nominal type or vtable. A new top-level TypeId
  is only for new language semantics. The tag budget has an owner (DI3).
  [Item_Boxing §5, R10]
- **D2.1.4 — Construction is canonical.** Each value has one encoding in each
  build. A container constructor never adds a TypeId with OR. A leaf
  constructor uses the canonical constructors. A null pointer becomes the
  canonical null Item, never a zero-word container. [Item_Boxing §6.3]
- **D2.1.5* — Design the lifecycle with the storage class.** A storage class
  is designed only when its allocation, reclamation and equality are stated
  together. A new tag, sentinel or packed encoding is not permitted until an
  assertion pins it. [Item_Boxing R7, R8]
- **D2.1.6 — Some decisions are one-way doors.** These decisions are accepted
  deliberately: a non-moving heap, no pointer compression, 8-byte references,
  the low-address container model, and the AArch64 top-byte rule (DI1).
  Confirm them again before work on the GC, a sandbox or a new port.
  [Item_Boxing §7.3]

### D2.2 Numbers

- **D2.2.1 — Common doubles are inline.** A common double is self-tagged: its
  own bit pattern is the Item. The tag partition keeps the space for this
  test. A subnormal or colliding double is boxed through a number home. One
  canonical mask identifies inline doubles. [Double_Boxing; Stack_API inv. 23]
- **D2.2.2 — The int carrier has a private native lane.** A finite `int`
  packs into the 56-bit payload. Poison (`int.nan`, `int.inf`) is not packed;
  it stays an inline IEEE double. Thus an INT-tagged Item always decodes to a
  finite value. The native lane is `i64`. Its sentinels are at the
  two's-complement extremes, so `neg` and `abs` need no branch. Lane
  sentinels are private to the lane (DI4). [Int_Type §5.1]
- **D2.2.3 — Wide scalars have no inline form.** `i64`, `u64` and cold doubles
  have no inline form at any magnitude. Thus no representation branch depends
  on the value. They follow the scalar-home ownership rules (D6.2). A datetime
  is an object: GC-owned when it is dynamic, Input-owned when it is static.
  [Stack_API §15.1, SF16]
- **D2.2.4 — Decimal tiers are invisible.** A decimal Item points to a
  `Decimal` wrapper. The wrapper records the storage kind (fixed, extended or
  BigInt) and contains the libmpdec value. The libmpdec value describes only
  the number. It never selects the Lambda tier or the BigInt identity. Thus
  source-level tiers stay invisible, and a BigInt stays distinct without a new
  tag. [S4.6.1, S4.9.1]
- **D2.2.5 — In MVP-Lmd, Lambda `int` is a private subtype of JS Number.** The
  scope is the experimental MVP-Lmd JavaScript profile. There, finite JS
  integers can use the INT Item and the native `i64` lane. JavaScript sees only
  Number, also in
  mixed compares, storage and calls. Integer code needs range and signed-zero
  proofs or guards. A result that escapes widens with JS binary64 semantics
  before any Lambda saturation. The existing encodings and the host numeric
  FFI rule do not change. [USER 2026-10-07; JS_MVP_Lmd §9]

### D2.3 Representation discipline

*Never recover a decision from emitted MIR register classes.*

- **D2.3.1 — Boxing is representation only.** Boxing never changes semantics
  (S1.6). Each box/unbox pair gives back the same value exactly, also for
  poison and for null sentinels.
- **D2.3.2 — A typed function has a native entry and a boxed entry.** A typed
  function compiles to a native entry and a boxed entry (D7.3). An untyped
  function has only a boxed entry. A closure keeps its captures as `Item`
  values and unboxes them when it reads them. A union-typed parameter is
  never unboxed. This is a representation decision, not a soundness decision.
  [DF1, DF15, DF §1.3]
- **D2.3.3* — Never cast an Item to a container pointer blindly.** A blind
  cast is a defect. A container-typed parameter unboxes through a helper that
  checks the type and gives NULL on a mismatch. A container return boxes
  through the canonical constructor, so NULL becomes `ItemNull`, never the
  zero word. [DF2, DF4, Nullable §7.2]
- **D2.3.4* — Four facts have four owners.** The AST `Type*` owns the semantic
  contract. The lowering analysis owns the planned representation. `MirValue`
  owns the emitted representation, its provenance and the full `Type*`
  contract. Its MIR type owns only the physical register class. A boxed Item,
  the int lane, Lambda `i64` and a machine quantity can use the same MIR
  register type. Thus expression lowering must not probe MIR register types.
  Machine representations are separate from Lambda representations. The
  canonical representation of a Lambda type is never a machine
  representation. [Lane §1–§4.2]
- **D2.3.5* — A carrier conversion is not a semantic coercion.** One router
  changes only the carrier. Its key is the source representation, the target
  representation and the contract. An unsupported pair fails closed. No
  identity conversion crosses between a machine quantity and an int-lane
  value with the same bits. [Lane §4.3]

### D2.4 The nullable native lane

- **D2.4.1* — An optional keeps a native carrier.** `N(T?) = N(T) |
  NULL_LANE(T)`. Where a representation is selected, an optional keeps its
  native carrier. These rules apply:
  - `box(NULL_LANE(T)) == ItemNull`;
  - code tests the sentinel before use (DI5);
  - a plain `T` carrier never holds a null sentinel;
  - the full type contract selects the representation, never the TypeId
    alone;
  - numeric sentinels are never roots (DI6). [Nullable §1–2]
- **D2.4.2 — Each lane has one null code.** The int lane uses the `ItemNull`
  word as its null code, so box and unbox are exact. `bool?` uses one byte
  with three values. Sized integer optionals widen to the `i64` lane. `float?`
  uses a reserved NaN payload; the null test is a bit compare, never IEEE
  `==`. `i64?` and `u64?` have no spare one-word code. In expressions and at
  dynamic boundaries, they use an ordinary Item. In persistent native storage,
  they use a destination-owned `TypedItem` (DI9). Wide optionals do not use
  the unboxed wide arithmetic path. [Nullable §3–4]
- **D2.4.3 — Representation follows the full inferred contract.** An indexed
  read with an unproven index infers `T?`, not `T` and not `any`. A
  flow-sensitive proof can use the payload directly. It never changes the
  public inferred type. `any`, `number`, `integer` and mixed unions stay
  boxed. [Nullable §5]

### D2.5 The error-free lane

*This is the representation side of the acceptance rule (S7.8.1). DI5 and DI8
do the same for null.*

- **D2.5.1 — No native lane holds an error.** Native array storage, packed
  fields, native locals, unboxed parameters and returns, and their scalar
  homes are free of errors by representation. They have no Item word for an
  error, and no lane encoding is spent on one. An in-band error sentinel and
  a side table of errors are both rejected designs. A lane that must be
  decoded for errors is a boxed Item in disguise. [S7.8.1; IEH I1, TE-17]
- **D2.5.2* — The emitter keeps errors out of unboxed operators.** No unboxed
  operator receives an error operand. The emitter guarantees this; the
  runtime does not. No skip target, landing pad or error edge is emitted
  inside a native lane. This rule covers errors and defects. System faults
  have no type; they unwind through a recovery frame (D6.3.6). [IEH I2, TE-15]
- **D2.5.3* — Lane entry needs a static proof.** The destination contract
  decides admission (S7.8.1). A source that is proven infallible enters the
  lane without a branch. A source of type `T | error` is not eligible; it
  stays boxed until code discharges the error. A deferred initializer or a
  whole-variable reassignment can bind an error and continue (S7.7.2v2,
  S7.7.4v2). Thus its annotation alone does not prove later uses clean.
  Reassignments and joins must update the proof before a native use. The
  gate is the `may_defect` fact, and it fails closed (D7.1.3). An unknown fact
  means "can fail", so the effect split is a prerequisite for lane routing. An
  unannotated literal that boxes only because its fallibility is unproven
  must get a diagnostic. [TE-17, TE-18.1–TE-18.2; IEH I3, I4]

### D2.6 Scalar ownership

- **D2.6.1 — No standalone scalar cell in the GC heap.** An `INT64`, `UINT64`
  or `FLOAT` payload is never allocated in the GC object zone. An Item with
  one of these tags can never point into it. The shared allocation point
  enforces this in every build, and a violation is fatal. A GC object can
  contain scalar words; it is never a scalar cell. This makes a class of
  lifetime bugs impossible. [SG1, SG4]
- **D2.6.2 — A container owns its wide scalars.** A raw wide-scalar Item in a
  container points only into the buffer of that container. A header is never
  reallocated away from its identity. A wide optional array element or packed
  field holds no raw scalar Item; it is an inline, destination-owned
  `TypedItem` (D2.4.2). [SF15]

## D3 Containers and Shapes

### D3.1 The container model

- **D3.1.1* — One container hierarchy: map, array, element.** Containers form
  one inheritance chain. `Map` is the base: a header and the attribute face.
  `Array` extends `Map` with its items; `ArrayNum` is beside `Array`.
  `Element` extends `Array`. Thus the attribute face is at one offset in every
  container, and a cast to an ancestor is always valid. The base is the
  property bag that all values share; JavaScript calls it an object. A Lambda
  object is a map, array or element whose type descriptor has a nominal
  record. Nominal status is a property of the descriptor, not a separate kind
  or TypeId. A header bit can cache it. A shape transition from a declared
  shape keeps the same nominal record, and `is T` compares that record. A
  nominal name is the name of the type; it is never matched against an
  element tag (S11.3.1v2). Only a structural element pattern enforces a tag.
  Whether a declaration can also name a tag is open (DO31). The container
  header is public ABI for MIR and the GC; static asserts pin its size and
  offsets. [OB1, OB2, OB4, OB13–OB16, OB20, OB22, OB23]
- **D3.1.2* — Sealed types permit cached layout.** A nominal type does not
  change during evaluation (S2.1.4). Thus the JIT can cache its method table
  and its declared field offsets for the life of the program. The declared
  prefix of an instance never moves, so direct access stays valid after an
  extension (D4.2.5). A type alteration (S2.1.5) builds a new instance, so it
  makes neither cache invalid. [OB15, OB18]
- **D3.1.3* — Node identity is data, never an address.** Do not use the
  address of a container as its identity. A COW detach gives a new address to
  the same node (S5.1.4v2). The carrier of the identity and the operations
  that keep it are open (DO25). [OB10]
- **D3.1.4* — A bound method is a closure over its receiver.** A bound method
  is an ordinary function value (D7.2.1). Its closure environment is the
  receiver, captured by value. The compiled entry takes the receiver first,
  and the method arity does not count it. No unbound method value is visible
  (S12.3.3v2). A `pn` method gives no value; the emitter rejects a bare
  reference to it at compile time. [OB6]
- **D3.1.5* — A JavaScript object is a nominal Lambda map.** The descriptor of
  every JavaScript object has a nominal record: its class. Thus every JS
  object is a Lambda object (S2.1.1v3), and a JS array is a nominal Lambda
  array. The semantics behind the record stay per language (D1.3). A JS class
  finds its members through its prototype chain. A Lambda type finds them
  through its method table. Map machinery that only JavaScript uses stays
  limited to plain maps. [OB21]

### D3.2 Arrays

- **D3.2.1 — There are three physical array forms.** An array is a boxed
  Array of Items, a native Array, or an `ArrayNum`. A native Array has one
  uniform native slot for each element, selected by its descriptor. An `int`
  element uses the `i64` lane and maps to IEEE at print and box boundaries.
  One shared descriptor resolver selects the storage for MIR, map layout,
  arrays and guests. Shapes keep the result as an immutable
  `LaneStorageDesc`. [Nullable §6; Int_Type §5.8]
- **D3.2.2* — ArrayNum is non-null and invisible.** An admitted null store
  demotes an `ArrayNum` to a native `Array<T?>`, once and atomically. An
  `int[]` rejects the null store. A covariant `int[] → int?[]` assignment can
  share the buffer under COW. `pn var` parameters are invariant. An `ArrayNum`
  must be the same as an `Array` for equality, `type()` and iteration. An
  `==` that depends on the representation is a bug. [Nullable §6.3–6.4,
  CW16.1]

### D3.3 The append discipline

- **D3.3.1 — The destination selects the append.** There are three appends,
  and they are not interchangeable:
  - The content append applies the S2.6 normalization. It drops `null` and
    `""`, splices a list, and joins adjacent strings or binaries. Use it only
    for element content and script top-level content.
  - The sequence append builds lists and array literals. It splices a list,
    and it stores every other value as it is.
  - The verbatim append stores the item as one value. Use it for argument
    lists, pairs and every single-value slot. A list becomes its array image
    first (S2.5.6).

  What the site builds selects the append. A format switch or an ambient flag
  never selects it. The kind of a value at run time decides whether a list
  spreads, never the syntax around it (S1.6). Every input parser gives
  normalized element content (S2.6.1v2). Builder and editor writes to element
  content use the content append (S2.6.5). [LR09-R3; Design_Syntax §7.27]

### D3.4 Shapes

- **D3.4.1v2 — Shape layout is ABI.** A map or element shape is a `ShapeEntry`
  chain on its type. Map data is a packed struct with precomputed offsets.
  A map literal or element/object attribute list rejects repeated explicit
  names as a compile error. Computed entries and spreads may replace earlier
  values; the first insertion fixes field order, so a shape has one field per
  effective key (S2.3.1v2). Transpiled direct access and the runtime read and
  store paths must read the same bytes. [Shape_Pool §1, Transpile_Map]
- **D3.4.2 — Shape identity is structural.** A shape is identified by its
  ordered sequence of field names and TypeIds. That sequence is its path in
  the transition tree (D3.4.3). Field order is significant. Offsets come from
  the sequence; they are not part of the identity. [Shape_Pool §3,
  Shape_Transitions §3]
- **D3.4.3 — Shapes share through a transition tree.** Maps and elements
  share types through a transition tree that grows one field at a time. Each
  `Input` owns a tree. Each evaluation context owns a runtime tree for each
  heap generation. An add follows the edge for the field name, key kind and
  value TypeId, or it makes the child once. The field list of a shared type is
  immutable. An add with no usable edge moves the map to a private type. A
  tree can grow from an external parent type, but it never writes to that
  parent. A child keeps the nominal record of its parent. A type-changing
  write follows a retype edge in the same way. The tree has bounds; past a
  bound, a map keeps a private type. Sharing is memory policy, never
  semantics. [Shape_Transitions §2–§7; JS Tune10 §9.3, §9.9]
- **D3.4.4* — Shape names follow the name identity rule.** A shape-entry name
  is its resolved namespace and its normalized spelling (S8.2.2v4). Equal name
  IDs prove an equal spelling. Different or missing name IDs prove nothing;
  the key kind, namespace, length and bytes decide. JavaScript keys use the
  same space under the host namespace `js`: symbols and private names get the
  namespaces that the runtime already keeps. A symbol or private entry is
  identified by its name ID and key kind, never by its description. Copies and
  transitions keep the name ID, the key kind and the namespace. [NI10, NI13,
  NI15, NI17, NI18]
- **D3.4.5 — The exact shape always describes the stored bytes.** A
  compatible write stays on the direct path. An incompatible write rebuilds
  the shape and repacks the data as a transaction: if it fails, nothing
  changes (S11.4.1). A `let`-bound container never transitions. Only `var`
  and procedural code can cause a transition. An annotated root must continue
  to conform. [TE §6 B7b, Transpiler DD3]
- **D3.4.6 — Shapes carry the lane descriptor.** A shape carries the
  immutable `LaneStorageDesc` from the shared resolver (D3.2.1). A change of a
  field contract computes the layout again. A null that is written into a
  `T?` field does not. [Nullable §6]
- **D3.4.7 — JavaScript class metadata refines runtime shapes.** A runtime
  JavaScript type can carry one immutable class-metadata record, selected
  before publication. The record holds only stable IDs, flags, policies and
  static operation tables. It is part of the runtime type, transition and
  cache identity, but not of the structural layout identity. A JavaScript
  accessor is a virtual, object-local shape entry that uses no data lane. Only
  the JS property kernel dispatches it. [JOP1–JOP5, JSCU33]

### D3.5 Virtual output lists

- **D3.5.1* — A virtual output list carries its builder.** The content
  destination of a PDF file is a list with a builder. A representation flag
  sends content appends to that builder instead of to an Item array. The flag
  is separate from the list kind bit. Normal lists keep their storage. No
  output context or allocator context is installed. The encoder reads
  borrowed storage and writes bytes; it keeps no source Item edges. Document
  construction stays in Lambda. A result-demand path passes a rooted
  destination explicitly (D8.2.6). A checked append returns a separate
  completion status, and a handler can rewind its bytes. Completed files
  compose as bytes. [PDF_Output §5.4–§5.6]

## D4 Types

*Types are first-class values: they compose, they are inferred, and they are
reflected and matched (S10.1, S11).*

### D4.1 Type values

- **D4.1.1* — A type value is a `Type*` graph.** A type value is a node of a
  `Type*` graph under one TypeId, with a kind field. Composite, pattern,
  range, constrained, binder and binary-schema types share that tag; they use
  no tag budget (D2.1.3). The `Type*` graph is the semantic authority for
  every contract that the compiler carries (D2.3.4). A TypeId alone is never a
  contract. For example, `T[]` keeps its element, leaf element, rank and leaf
  carrier in the graph. No array path recovers those facts from an erased
  array tag. [S11.1.1v3, Lane §1]
- **D4.1.2 — Types compose as values and compare by representation.** Types
  compose as values: union, optional, array and constraint. They compare by
  normalized form, not by semantic equivalence, which is undecidable with
  constraints. Union normalization uses a canonical order (S5.5.2). That order
  is open (DO22). [C8.5-4]
- **D4.1.3 — Reflection uses operators and functions.** `type(x)` gives the
  type value, and `name(T)` gives its name. `match` and `is` use the same
  runtime membership operation (D4.2.1). One classification mechanism serves
  the whole surface. [C9a, TE-6]
- **D4.1.4 — A type binder has a stable slot.** A binder site stores its
  written bound, its name and a stable slot in its signature. A bound-name
  reference stores the slot and a copy of the bound, never a pointer back.
  `x: as T` elaborates to the same site with an error-free `any` bound; it adds
  no type kind and no specialization key. Each interpreter or MIR activation
  owns an exact, rooted environment for its binder values. A binder parameter
  uses the boxed entry until a specialization proves a direct path. A cached
  certificate never skips the walk that publishes a binder slot.
  [S11.4.8v2, D4.3.3]

### D4.2 Subtyping and validation

- **D4.2.1 — One subtype model, three operations.** The three operations share
  one numeric foundation. They are never treated as the same operation:
  - `subtype(S, T)` is static. It drives inference and static admission.
  - `matches(item, T)` is runtime membership. It drives `is`, `match` and the
    validator checks.
  - `checked_convert(item, T)` belongs to the explicit conversion functions,
    which own lossy conversions. [TE-6]
- **D4.2.2 — `<:` spells the subtype relation.** `<:` takes two type values
  and returns `bool`. It is not the magnitude operator `<`, and it does not
  test value membership. It uses the static value-set relation without
  conversion, including the nominal base walk (S11.3.1v2). Declared-boundary
  compatibility permits a conversion, so `<:` must not use it. Function-type
  operands are rejected until a ruling defines their variance. [S1.7, S6.1.1,
  S11.1.4v2, S11.3.1v2]
- **D4.2.3* — The validator enforces user-defined types at run time.**
  Validation is deep, and it happens at the first crossing. A failure gives a
  rich error with a validator path. A deferred initializer or a whole-variable
  reassignment binds that outcome and does not leave its block (S7.7.2v2,
  S7.7.4v2). Other boundaries use the outcome by their own contracts. A
  constrained type (`T that …`) enforces only its base for now. `is`, the
  runtime, the emitter and the validator must use the shared foundation.
  [TE-10, TE-6 P5, TE-18.1–TE-18.2]
- **D4.2.4* — Declared and effective types are separate facts.** The
  declaration is on the binding node, and inference is beside it. An
  annotation is a strict admission contract (S11.4.1v5). The effective type of
  a deferred initializer or a reassignment includes the validation-error
  alternative (S7.7.2v2, S7.7.4v2). Track the possible outcomes through writes
  and joins, and do not widen the declared target. Inference never replaces
  validation. It never selects a native representation for an outcome that
  can hold an error. [TE-1, TE §8.1, TE-18.1–TE-18.2]
- **D4.2.5* — A crossing into a named map contract reifies its layout.** A
  named contract fixes a physical layout: field offsets and storage classes.
  Direct field access indexes by that layout. Thus a boundary check can be
  skipped only if the source is proven to have that layout already. Semantic
  compatibility is not sufficient, because the same value can have two
  physical shapes. A value gets the proof in two ways only: it adopts the
  contract at construction, or it crosses a boundary that reified it. Each
  user of this rule finds the contract through the non-null arm of its
  spelling (`N?`, `N | null`, `N^`, `N | E`). Errors stay in Item storage and
  never get the layout. The open `map` fixes no layout. For an open nominal
  instance, the contract fixes the declared prefix. A violation is a
  memory-safety defect. The rule holds at each rank of `N[]`. The proof is
  transitive through a verified carrier: an element or a field read from a
  certified carrier has the layout proof while the verification is valid
  (D4.2.6). [S11.1.1v3, S11.4.6v4, Tune19 §11.5, Tune20 §T20-6a, OB16,
  TE-13.4]
- **D4.2.6* — Verified once, valid while unchanged.** A declared type on
  `let`, `var`, a parameter, a return or a nominal element binding is verified
  at the crossing. The verification stays valid while the data does not change
  (S11.4.10). Under a nominal contract, each required field is present; the
  container stays open beyond the declared prefix (S11.4.6, S2.1.4). Native
  lanes store exactly the declared layout: that is the ABI. Thus a reader with
  a verified contract reads a required field by offset, with no presence test.
  Structural composition has no constraint. A value built by a host is outside
  this rule until it crosses a boundary that admits it. [TE-19, S11.4.10]

### D4.3 Inference

- **D4.3.1 — Inference never changes the result of a correct program.** Take
  a script with no type error. Erase all inferred types and run it boxed: the
  result must be the same (SI3v2). A differential harness between boxed and
  JIT runs checks this. Inference is visible statically: better precision can
  reject a script at compile time, at once, with no warning period. Strictness
  is per surface. The Lambda lane is strict by default. Its relaxed mode
  reports the same diagnostics as warnings, and it drops a failed annotation
  to the inferred type (SI14). The LambdaJS lane is lenient by default. [B7,
  TI6]
- **D4.3.2 — Inference selects an implementation, never a contract.**
  Entry-shape inference and body inference are separate products. Float
  arithmetic on a parameter does not change the parameter type. Precise
  inference can find an error component and make a static obligation (E208).
  It never makes a runtime obligation that the erased run does not have.
  [DF12, DF13, TE-13, TI6]
- **D4.3.3 — Inferred narrowing dies with its binding.** Inferred element-type
  narrowing belongs to one binding scope. It is never written into the
  container. An explicit `T[]` contract that is enforced is different. It can
  attach an immutable certificate that proves the rank, the leaf contract and
  the physical carrier. The certificate is valid only while the contract and
  the carrier both match it. A semantic certificate on boxed slots cannot
  authorize a raw or compact-lane read. A transform keeps a certificate only
  after it makes the matching carrier. An unchecked change of representation
  clears the certificate. [S11.4.1v3, elem-type invariant]
- **D4.3.4 — System-function result relations are registry metadata.** A
  sys-func registry row records a result relation and a source-argument index.
  The AST builder applies the relation to the full argument list. A relation
  never exposes a type name, and it never changes native dispatch. A row falls
  back to its declared open result where the native operation cannot prove
  the relation. [S11.4.9, S17.2.1]

### D4.4 Binary schemas

- **D4.4.1* — Binary schemas compile to immutable descriptors.** A binary
  schema compiles to immutable field descriptors that its module owns. One
  schema-backed element backend serves byte buffers and declared native
  layouts. One manifest generates the native bindings and the Lambda
  declarations. Admission checks the binding ABI, the manifest revision, the
  field kinds, the typed targets and the span contracts. A mismatch rejects
  admission. A byte buffer cannot grant native memory access.
  [Binary_Schema §10]
- **D4.4.2* — A native schema view can be made invalid.** A native view keeps
  a lifetime control. The host pins the admitted storage, and it makes the
  view invalid before it releases the storage. Every subview shares the
  control. Each access checks validity before it touches storage. Counted
  spans check their counts against the host extents. Declared native Item
  fields are traced precisely; arbitrary native memory is never scanned. No
  native address or ownership transfer is visible to a script. [D5.5.1,
  D6.3.3–D6.3.4; Binary_Schema §10]

## D5 Memory

### D5.1 Tiers and mechanisms

- **D5.1.1 — There are four content tiers.** Content lives in four tiers: the
  GC heap (runtime values), the Input arena (parsed documents), the AST and
  const pool, and the NamePool (interned names). The collector does not see the
  non-GC tiers. A pointer range test is the only gate, and tracing stops at a
  pointer outside the GC heap. Each content tier maps to exactly one
  allocation mechanism (D5.1.4). [GC1 §2.10.4; Mem_Heap §1.1]
- **D5.1.2 — Static Mark data is born shared and immortal.** The builder sets
  the shared COW state at construction. The first mutation makes a copy in
  the runtime heap. A read of a wide scalar from a container takes a reference
  if the storage is immortal (const pool or Input arena); otherwise it copies.
  The storage class decides, not mutability. [CW8, SF16]
- **D5.1.3 — Input data is outside GC rooting.** Input and format allocations
  belong to a pool or an arena. They are outside GC rooting. A new boundary to
  the GC heap there needs an explicit audit. [CR8]
- **D5.1.4* — There are four allocation mechanisms.** Heap bytes come from
  exactly four mechanisms: tracked raw allocation (`memtrack`), the GC heap,
  an arena and a pool. A fifth mechanism needs a new ruling.
  - An arena allocates in sequence and releases in batches: the whole arena,
    or a tail region (mark and rewind). An arena block is never freed alone.
    It can only be retained for reuse in the same arena: retain, never
    discard. DOM node retirement is the approved user of retention. A rewound
    tail has one owner.
  - A pool allocates and frees blocks one at a time. Owner-wide teardown is
    its safety net. A pool block operation uses no global registry, hash map
    or lock.
  - Arenas and pools have one writer thread and many reader threads.
    [Mem_Heap §1.1–§1.2, §4–§5; MP-12, MP-15, MP-21]
- **D5.1.5* — Select the mechanism by lifetime.** A string builder is
  standalone (tracked memory; the result is copied) or owner-backed
  (allocated from the destination; no copy). Select by whether the destination
  lifetime is known at build time. A pool-backed builder grows by
  reallocation. An arena-backed builder grows by tail extension, and it never
  reallocates. An interning store is append-only, so it uses an arena.
  [Mem_Heap §2.3, §4.3; MP-19, MP-20]

### D5.2 Memory contexts

- **D5.2.1 — `MemContext` owns every allocator.** Each arena and pool is bound
  to a named context when it is created. The context records the role,
  label, parent and thread mode. No allocator exists outside the graph.
  Teardown releases each owned allocator, or drops its reference (D5.2.3), in
  reverse dependency order. Then it proves that no allocator child remains.
  System allocations use the checked `memtrack` API. A pool gets its bytes from
  VM extents that it owns. Hot allocation paths do not take the registry lock.
  [Memory_Context §2–§7; Mem_Heap §1.3, §5; MP-16]
- **D5.2.2 — Documents get attribution; some also get reclamation.** A
  per-document sub-context keys allocators to a document URL. A Radiant
  document can be reclaimed as one unit. Lambda `input()` data is coupled to
  the GC, so it gets attribution only. `MemContext` is the one coordinator for
  page-allocation failure and memory pressure. An allocation failure enters its
  retry-and-reclaim path before it returns `NULL`. [Memory_Context §15]
- **D5.2.3* — A shared allocator has a reference count.** A shared arena or
  pool has an atomic reference count. The creating context holds the first
  reference. The allocator is destroyed at zero. The count is the only
  allocator field that more than one thread changes. A reset, or a rewind of
  published data, needs exclusive ownership. Immortal storage holds a
  permanent reference. [Mem_Heap §1.4; MP-17]
- **D5.2.4* — Allocation tracking is a diagnostic.** Tracking is never the
  allocation mechanism. Release builds turn `memtrack` off by default.
  Per-allocation records exist only in debug builds. No tracking mode puts a
  global lock on a release hot path. Outside the memory implementations in
  `lib/`, code must never call libc `malloc`, `calloc`, `realloc` or `free`. It
  must use the `memtrack` API or an owner API (GC, arena or pool). A source
  audit enforces this. [Mem_Heap §2.1, §2.4, §5.2; MP-13, MP-14, MP-18, MP-21]
- **D5.2.5 — A pool can own an external resource through a cleanup.** A
  cleanup is a callback that runs once, newest first, when the pool releases
  its blocks. It is lifecycle only, not a fifth mechanism. Every `Input`
  belongs to the pool that holds it. It has its own context, and it registers
  its release as a cleanup of that pool. A cleanup called from a registry
  cascade must not enter the registry again. [Memory_Context §16]

### D5.3 Garbage collection

- **D5.3.1 — The collector is a non-moving, two-zone mark-and-sweep.** Objects
  never move: Item pointers are stable, and there are no write barriers. Only
  the data zone compacts, with one pointer fix for each surviving object. Thus
  the fix cost follows the survivors, not the references. The object zone uses
  size-class free lists. The data zone is a bump allocator that resets at each
  collection. Only the owning GC heap releases its regions. A copying GC and
  handle tables are rejected. [GC2 §4, §5, §12]
- **D5.3.2 — Closures and virtual maps are traced precisely.** A closure
  environment records its field count. A virtual map traces and finalizes
  through vtable callbacks. Those callbacks never allocate, and they never
  enter script. [GC2 §8, JA6]
- **D5.3.3 — A null-typed shape lane is traced as a possible pointer.** The
  store path can put a container pointer into a `null` lane in place. A
  literal shape that many instances share keeps `null` for the others. Thus
  the declared lane does not describe the word. The collector marks the word
  when it points to a managed object. A real `null` is zero and marks nothing.
  [GC2 §8; DO30 record]

### D5.4 Copy-on-write

- **D5.4.1 — COW uses one shared bit.** The shared flag is monotonic and
  idempotent. A copy sets its own state explicitly; it never copies the flag.
  A false "shared" costs a copy; a false "unique" is a semantic bug. A
  saturating count is a compatible extension for the future, gated on
  live-corpus counters. The same bit identifies values that cross isolates.
  [CW3, CW6, CW3-C]
- **D5.4.2 — A COW copy is one level.** A copy duplicates its own storage and
  marks its container children shared. Raw mutators have no policy. Lambda
  lowers through COW wrappers; JavaScript lowering never uses them. A raw hot
  path never tests the language mode. The unique fast path is one byte load,
  one test and one branch. [CW9, CW21, CW1/CW2]
- **D5.4.3* — Copy paths stay rooted.** Each copy path roots its source, its
  replacement and its owner chain precisely. It reloads after a possible
  collection. Forced-GC stress is a permanent gate. [CW20]
- **D5.4.4* — A place handle can borrow its place.** A place handle is `var h
  = root.path`, or a handle that the compiler makes for a path that is spelled
  many times. It can borrow its place if the sharing stays invisible under
  S9.1.2. A handle carries facts about its place: identity, layout, presence and
  uniqueness. The facts hold only while the place and its ownership do not
  change. An event that can replace, reorder, share or give away the place or
  a prefix ends the facts that it can affect. Data-zone pointers are never
  facts; reload them after an allocation point. Three conditions are fixed:
  - one static decision serves both tiers;
  - the bind runs the runtime spine test;
  - each path that writes through the handle stores it back before its next
    use, or share-marks it.

  The handle shapes and the invalidation table are implementation details
  (CW36, CW37). [CW34]
- **D5.4.5 — A place copy is share-marked if its place can change.** A place
  copy is `let`/`var h = root.path` on a mutable root. It is share-marked at
  its bind if the place can be written while the copy is alive. Decide this
  statically for each binding. These events in the live range of the copy are
  triggers:
  - a write through the root or a prefix of the path;
  - the root as a `var` argument;
  - an escape of the copy.

  Otherwise the bind is a free borrow. A move-out
  copy, whose place is overwritten later in the same statement list before
  any other use, also binds as a borrow. [CW24v3, CW35]
- **D5.4.6 — A `var` argument uses a caller-owned home.** A place that is
  passed to a `var` parameter uses an Item home that the caller owns and
  roots, and evaluated path keys. The callee publishes its final value
  through the home. The caller writes it back through the original path, also
  on an error result. No address into a movable data buffer crosses the call.
  Scalar and container places use the same protocol. [CW33v2; USER
  2026-11-03]

### D5.5 The Radiant seam

- **D5.5.1 — One substrate, two policies.** Lambda and Radiant share one
  system-allocation substrate. Lambda traces. Radiant uses arenas as regions,
  type-stable pools, generation handles and RAII, and it is never collected.
  The document arena is the cycle collector. Arenas have batch lifetime only,
  plus retention for reuse (D5.1.4). Each scratch stack owns its own arena.
  The seam contracts are: pin, check the generation, copy as a value.
  [Memory_Model §7; Mem_Heap §4, MP-15]
- **D5.5.2* — Radiant never keeps a GC pointer.** Storage that outlives the
  current call never points into the GC heap. To keep a GC value, Radiant
  copies it into the document arena. A GC value passed for reading is a short
  borrow for the work that reads it. A reference under a registered GC root
  that lives at least as long as the document is exempt. Borrow to read; copy
  or root to keep. [Memory_Safety_Template §2.3]
- **D5.5.3 — DOM construction copies; presentation retains.** View-mode DOM
  construction deep-copies ordinary elements and their content. Each
  presentation occurrence owns its native parent and sibling links. Normal
  Lambda evaluation keeps ordinary value sharing. The copies keep the
  source-to-result mapping. Incremental presentation retains structure: a
  regeneration can patch a retained subtree in place, and restyle, reflow and
  repaint reuse every valid structure. This ruling does not constrain style,
  layout or rendering. [Reactive_UI §5.3, §10.1]

### D5.6 Name identity

- **D5.6.1 — A `NameId` is property identity.** A property name is
  identified by a `NameId`, never by a `String*` address. The NamePool
  resolves names. Pointer equality never compares properties, shapes,
  transitions, symbols or private names. A JavaScript `Symbol` is the one
  exception at the value layer: its pointer is its JS identity, and its name
  ID stays its property identity. String keys use the canonical UTF-8 or
  lossless WTF-8 form (S8.2.2v5). No Unicode normalization takes part in
  identity. Different name IDs from different pools never prove different
  keys. [NI1–NI4, NI16, JSCU57]
- **D5.6.2 — Evolve the NamePool; do not replace it.** The first definer wins,
  and lookup checks the parent first. One identity scope has a sealed static
  root and one dynamic child for each owner thread. Static linking uses a
  sealed key image and a per-context ID table. Dynamic IDs are never persisted
  and never emitted as MIR immediates. Document fields stay without IDs unless
  a retained schema resolves them. `eval` and the REPL use the same pipeline.
  [NI6, NI7, NI10, NI12, NI13]

### D5.7 Const pool and MarkPack

- **D5.7.1* — One binary encoding has four users.** The const pool,
  documents, interchange and messages use one encoding. It has four sections:
  names, types, data and code. Containers materialize a spine and map their
  leaves; in-place relocation is rejected. A materialized template is a
  permanent COW source. An emitter allowlist is the only legal surface of MIR
  operands. [CP2–CP7, CP25]
- **D5.7.2* — Sealing appends; stream bytes never change.** Two stream
  variants use one encoding: a bare stream, and a stream with a skip table and
  a block index. Validation has two tiers: full structural validation before
  any mapped pointer escapes, and magic and hash checks for trusted caches.
  Malformed input is an error, never undefined behavior. Functions, closures,
  virtual maps and native handles do not serialize. [CP18, CP20–CP22, §4c]

## D6 Execution State

### D6.1 Stacks

- **D6.1.1v3 — Three stack mechanisms have different owners.** The native C
  stack is invisible to the GC. Two watermarked side stacks hold values: the
  root stack, which the collector scans precisely, and the number stack,
  which it never scans. Activation stacks run suspendable bodies: a Lambda
  task, or a JS generator, async function or module body. These bodies run on
  their own native stack with their own root and number segments, and park in
  place, except when a closed MIR admission proves that a synchronous JS
  generator owns all state needed to resume in a precisely traced compact
  heap continuation. That generator returns from JIT code at yield without
  retaining a native frame; unsupported definitions retain the activation path.
  Parked-page reclamation uses a resident-byte budget. Memory pressure
  and collections batch reclamation; suspension alone does not evict by count.
  A parked activation contributes only its root segment, traced through its
  owner. LIFO lifetimes use the side stacks. Non-LIFO lifetimes own their
  scalars in tail regions of their own allocation. Other suspendable bodies
  are not transformed to suspend. [SF2, SF12, Stack_Frame §5.1, RA1, RA3,
  RA13–RA16]
- **D6.1.2 — Frames are static.** A function has a static frame size, no
  `MIR_ALLOCA` and two saved watermarks. Stacks use a virtual reservation with
  demand paging. Frame entry checks the limit explicitly. There is no hotness
  detection; the primitives are always cheap. Be as static as a dynamic
  runtime can be. [SF5–SF7, SF13]
- **D6.1.3 — Only upward flows and outliving stores re-home a value.** A
  downward flow never re-homes a value. An upward flow, or a store that
  outlives the frame, re-homes it. Suspension is a re-homing barrier.
  [SF19, SF20]
- **D6.1.4 — Cleanup runs before the watermarks are restored.** All `MAY_GC`
  cleanup runs before the root and number watermarks are restored. Each
  function has one epilogue. A frame-capacity failure fails closed through the
  failure lane of the language. [Stack_API §8.6, §8.10]

### D6.2 Scalar homes and wide returns

- **D6.2.1* — Wide returns use a companion lane.** A boxed return that can be
  wide is a pair: an Item and a raw 64-bit scalar. The scalar is live only if
  the Item is a pending Item, which has a reserved tag. A pending Item never
  lives in memory, and only one is live at a time. The caller resolves it at
  its first use, patches it on an escape and forwards it on a tail return. A
  typed fallible return uses a proven native success lane with a companion
  error lane. `T^` and `T | error` have the same outcome plan (S7.4.2v4). The
  scalar lane and the error lane stay separate. The birth site of the wide
  value selects its class statically, never a boundary. Generated functions
  have no trailing caller-home parameter. Their C-reachable wrappers use a
  context companion slot. A guest ABI with its own version can ask for an
  explicit payload owner; that is a native ownership boundary, not this return
  ABI. [RV1–RV13, Return_Value §2–§5; SG2]
- **D6.2.2 — A scalar home is a payload word owned by a destination.** A
  scalar home is a raw payload word in a separate slot space, never a GC root.
  A lane that nobody reads uses a discard home. Every ownership boundary has
  destination-owned scalar storage: array tails, typed fields, environments,
  module tables, and task and message records. A wide-capable mutable local
  also owns one slot for its scope, and an assignment copies the payload into
  that slot. These slots are not a return convention for generated functions.
  [RV14–RV16, Return_Value §4a; Stack_API §15.3]
- **D6.2.3* — Watermark ownership selects the wide-return path.** An entry
  that sets a number-frame watermark removes it at return. Thus it cannot
  return a pointer into its own extent, so Lambda and LambdaJS functions use
  the companion lane. An entry that sets no watermark, such as a C helper,
  allocates in the extent of its caller. It returns a wide scalar as an
  ordinary Item that is already homed for the caller. Ownership decides, not
  the implementation language. A loop that calls a wide-capable helper
  reclaims the number stack at its back edge. That reclaim is sound only if no
  loop-carried wide value is in the reclaimed extent (DO24). An unaudited
  callee means "can allocate". [RV14, RV14a, RV15, RVO11, Return_Value §4a]

### D6.3 GC rooting

- **D6.3.1 — Generated code uses safepoint-current canonical slots.** Each
  rootable binding has a stable home, and scratch homes are colored.
  Registers are a write-back cache between safepoints. The number of root
  stores follows the dirty live homes at `MAY_GC` boundaries, not the number
  of instructions. A function with no roots has no frame. [CR1, CR3]
- **D6.3.2 — A collection starts only in a `MAY_GC` call.** There is no
  asynchronous, signal-driven or background collection without stack maps.
  `MAY_GC` is the default. `NO_GC` is a claim that a checker verifies
  transitively. [CR4]
- **D6.3.3 — Native helpers root through slot-backed frames.** C and C++
  helpers use `RootFrame`, `Rooted<T>`, handles and `PersistentRooted` on the
  same side-root stack. Arguments are borrows that the caller roots. A helper
  can return a value safely. Statics use persistent roots. A handoff stays
  rooted all the time. Generated code writes back; native code writes through.
  There is one stack, one safepoint invariant and one collector interface.
  [CR2, RH1–RH8]
- **D6.3.4 — The representation class decides rootability.** Four
  representation classes decide rootability: boxed Item, raw GC pointer,
  non-GC scalar and raw non-GC pointer. The MIR register type never decides.
  Rooting policy and final store insertion belong only to `MirEmitter`. A
  transpiler reports semantic events. A guest is precise only if it emits
  through these primitives. [CR5, CR6, SF11]
- **D6.3.5 — Arguments use a fixed frame suffix.** A function reserves its
  largest overlapping argument arity as a fixed suffix of its side-root frame.
  Dynamic rooting for each call is rejected. Fixed global Item storage uses
  root ranges with an epoch guard. A merge of context stacks with different
  semantics is rejected. [Stack_API §17]
- **D6.3.6 — Recovery frames form one chain per activation.** Each activation
  has one LIFO chain of recovery frames; the base stack of a thread is one
  activation. Each boundary has a frame: execution entry, task body, handler,
  module transaction and guest entry. Each frame has an exact checkpoint of
  the side-root and number stacks. A landing restores the watermarks before it
  examines the fault. A frame can stay armed across a park on its own stack.
  No jump ever crosses a stack. [Rooting §2.1a, ER, Runtime_Async §4.5]

### D6.4 Context and globals

- **D6.4.1 — One `EvalContext` per isolate and one TLS root.** There is one
  long-lived `EvalContext` per isolate and exactly one approved TLS root. Only
  generated MIR receives the hidden `Context*` argument; native helpers read
  TLS. An eval thread keeps one context identity for its life. A context moves
  only by a quiescent handoff. [RG0, RG1]
- **D6.4.2 — Context state lives in opaque capsules.** Capsules are allocated
  when they are first needed. The context prefix that the JIT sees is frozen.
  Capsules never move, so rooted Item addresses stay stable. Root
  registration takes an explicit heap or context. It happens at construction,
  never in a repeated store. [RG2, RG5]
- **D6.4.3 — No context value is at a fixed address in code.** Module globals
  and other per-instantiation mutable cells live in per-context slabs. Code
  indexes them by a fixed slot and a module cell. Shared MIR contexts are
  sealed early. Reset becomes construction: reset, heap replacement and
  destruction are three separate, ordered contracts. [MT2, RG11]
- **D6.4.4 — Registries are frozen; hot paths stay lock-free.** Registries are
  global and immutable after publication. Diagnostics never feed semantics.
  There is one event loop per context. Thread infrastructure stays
  thread-local and is donated into context fields. A Lambda script has no
  mutable module-scope binding (D9.3.1); guests are exempt. Do not add a lock,
  an atomic read-modify-write or a coherence operation to a repeated path
  (DI12). [RG6–RG10, RG13, RG14]

## D7 Functions and Calls

### D7.1 The effect color

- **D7.1.1 — `fn`/`pn` is infrastructure.** `fn` and `pn` form a one-bit,
  declared effect system that the compiler checks (S12.1). Purity is a core
  AST flag; guest builders mark all code as effectful. Every callable surface
  carries the bit: sys-func rows, the runtime-function catalog and Jube module
  signatures. A host module states the color with the first keyword of each
  signature, and an `fn` cannot call a `pn` (S12.1.1v2, E224). A DOM row that
  changes state is a `pn`; a compile-time check keeps the keyword and the
  flag aligned. The effect walk visits both the attributes and the content of
  element literals. An unknown callee color keeps the runtime guard.
  [S12.1.1v3; U14, NM §6.2, Lang_Hosting §7.1, ES48]
- **D7.1.2* — The bit is load-bearing.** The bit is:
  - the soundness gate of the constant folder;
  - the license for stream fusion and parallel execution;
  - the condition for reductions;
  - the proof that `fn` regions do not suspend.

  It also makes the JS Promise membrane computable. Effects are not reified: a
  `pn` call executes, and there is no held effect value. Local mutation in an
  `fn` does not change its color. The bit alone does not prove that a read is
  invariant, that a loop is independent or that evaluation terminates.
  Optimizations keep sequential local dependencies and completion. A live
  capture that does not escape is never lifted into a shared or cached
  environment. [U26, PD11, K19, K23, Features §3.6; S12.1.2v3;
  Design_Procedural §6]
- **D7.1.3* — Other effects are separate facts that fail closed.**
  `may_await` (the call can park, collect and re-enter) and `may_defect` (the
  call can fault) are separate compiler facts. A missing analysis means "can
  fault", never "clean". `may_await` drives no transform and no tier choice.
  [IEH §5.3, ER-D13, RA1, RA11]

### D7.2 Function values and closures

- **D7.2.1 — A function value has a definition-site identity.** A function
  value is a GC object with its entry pointers, its type and its closure
  environment. Its identity is the static AST definition site, stamped at
  creation. An address is never its identity, because COW and recompilation
  change addresses (S5.5.1). The boxed entry is the universal target of a
  dynamic call (D7.3). [C8.7, DF7]
- **D7.2.2 — Dynamic calls use per-callee entries.** A dynamic call goes
  through the executable entry of the callee. A LambdaJS function has separate
  call and construct capabilities. Construction passes `newTarget` as an
  operand, never through a side channel. Catalog IDs, names, lengths and class
  labels are metadata; they never select runtime semantics. A declared native
  ABI can select a typed adapter once, at creation. The JS dynamic boundary is
  `Item* + argc`. Adapters that own scalar homes or rooted spans reach the
  same entries. The limit on source arguments is checked statically where
  possible, with a runtime backstop. Dynamic calls with named arguments, and
  dynamic calls to `var` signatures, stay rejected. [Function_Arg, LC
  call-ABI; JC1–JC4]
- **D7.2.3* — Closures respect capture permissions.** `fn` captures and every
  closure that can escape keep immutable Item snapshots from creation. A write
  to a capture is a compile error. A nested `pn` that is proven not to escape
  can access the live local `var` storage of its parent `fn` (S9.1.4v2).
  Sibling procedures share the same binding home. One analysis, shared by both
  tiers, decides the capture mode before layout. An unknown escape is not a
  proof of confinement. Live storage never outlives the parent call, and it
  uses precise roots. This does not change the LambdaJS capture semantics
  (D1.3). [Design_Procedural §5.3; C4, JSI5]
- **D7.2.4 — A closure environment is traced precisely and stays boxed.** The
  environment records its field count for precise tracing. Wide scalars live
  in its tail region. A closure has only a boxed entry. This is the most
  valuable relaxation for the future (D7.3.4). [GC2 §8, DF11, SF18]

### D7.3 Dual-function compiling

- **D7.3.1* — A boxed boundary plus a bounded set of raw variants.** A
  function has a boxed `<name>_b` entry and a bounded set of immutable raw
  variants. Each raw variant has one complete, exact semantic-shape key. A
  dynamic call reaches a variant only through the exact guard of its key. A
  statically proven direct edge is the equivalent proof. A multi-variant plan
  keeps `_b` as its complete boxed fallback. No variant is evicted, replaced
  or selected from runtime feedback. Variants are admitted in a deterministic
  order, up to a fixed limit. Partial specialization by parameter is rejected.
  A declared, native-eligible function has no boxed slow body. A slow body
  exists only if a parameter is specialized by inference, unboxing helps, and
  the set of callers is open. [S1.6, DF1–DF3]
- **D7.3.2 — The check lives in the callee.** A call site that cannot prove
  admission statically calls `_b`. The callee does the one boundary
  operation: one guard site, one correctness argument. A declared violation
  gives an error value. An inferred mismatch fails the guard and goes to the
  boxed body. An inferred guard is an exact shape test, not an admission.
  Unions and `any` get no test, and they stay `Item`. [DF4, DF5, DF8]
- **D7.3.3 — Correctness is relative to the source.** Neither generated body
  defines the semantics of the other. An input that fails the guard never
  reaches the unboxed entry. The unboxed entry has exactly two paths that give
  the proof: a statically proven site, or `_b`. A third path is a bug, not a
  slow path. [DF9, DF14]
- **D7.3.4* — Visibility decides which entries exist.** An exported function
  must have `_b`. A function that is not exported and has one exact visible
  call shape has only the unboxed entry: no guard and no slow body. A
  caller-side guard is permitted only as guard hoisting: the same predicate,
  with its failure edge on the `_b` path. A fixed-arity `pn` can have raw
  variants at a direct exact edge, also if it can suspend; it keeps `_b`. A
  `var` parameter is eligible when the edge carries its home exactly as `_b`
  does. A method is eligible when the exact receiver shape is proven at the
  edge. Closures, variadic functions and procedures that need a task context
  stay boxed-only. [DF15, DF16, DF11; O11v2]
- **D7.3.5 — One numeric admission rule.** At an implicit boundary, a static
  embedding of the whole domain admits and normalizes. A dynamic value is
  admitted only if the conversion is lossless. Admission is not a cast. [DF6,
  DF12, DF13]

### D7.4 Dispatch and the call ABI

- **D7.4.1 — No inline caches.** The Lambda lane and LambdaJS have no inline
  caches, feedback vectors or callee caches. Shared semantic dispatch is
  preferred to per-site caches. Specialized lowering has no dynamic dispatch
  sites. An open site uses runtime dispatch, and a hot open site is first an
  inference bug. Multi-version dispatch is a guard chain, never a patchable
  cache. Generated code stays immutable. The approved speed-up is a
  compile-time prediction with an inline guard and the shared kernel on a
  miss. A JS fast path is permitted only if a miss gives the same observable
  result as the kernel. [LC1v2]
- **D7.4.2* — Direct calls pass ABI operands.** A core direct call passes
  `Context*` and the arguments as separate operands. Returns use the companion
  lane (D6.2.1). `Item* + argc` is only the JS dynamic boundary. [LC
  call-ABI, RV1, RV10]
- **D7.4.3* — Every guest helper uses an explicit completion ABI.** A helper
  that can produce a Lambda error, a JS throw or rejection, or a guest
  exception returns an explicit completion. The shared ABI uses an
  ERROR-tagged Item or the declared companion error lane. A guest ABI with its
  own version uses only its declared completion adapter. No form uses a
  pending flag, a C++ exception, a non-local jump or an exception poll. MIR
  code tests the completion in the current activation. It routes the
  completion to the handler, the `finally` code, the cleanup or the error
  return. A raw scalar helper is permitted only if its catalog contract is
  infallible. `LambdaError` is the shared ERROR carrier. Native system faults
  are outside this ABI (D1.4). [S7.4.3, S7.4.4, REH-D3–REH-D12, JR3]

### D7.5 Concurrency

- **D7.5.1 — One scheduler per context.** Tasks share the context heap under
  one scheduler per context. A Lambda resume is a macrotask in FIFO readiness
  order. It never interrupts a JS job or its microtask checkpoint. The
  compiler rejects a `var` capture by reference into `start` (K13). This makes
  the thread count invisible to semantics. [K16, RG8, K13]
- **D7.5.2* — Workers are share-nothing isolates.** A worker is an isolate
  that shares nothing. Thread mode is the default, and process mode is
  optional. A message is a copy. Read-only structural sharing is only a
  representation. The COW shared bit also identifies values that cross
  isolates. Thread mode needs the audit of per-isolate runtime state and the
  cross-isolate lifetime ruling (DO20). [K31/K32, CW3-C]
- **D7.5.3 — A fault stays in one thread and one stack.** No jump crosses a
  thread or an activation. A worker publishes only a fault result that is
  already materialized. The boundary frames of a task live on its own
  activation, and they stay armed while it parks (S7.11.2v2). [ER-D1, ER-D11,
  Runtime_Async §4.5]

### D7.6 System built-ins

- **D7.6.1 — Public sys funcs return boxed Items.** At the runtime/JIT
  boundary, a public sys func returns a boxed `Item`. Collection results use
  the array carriers; there is no raw pointer convention. The return type
  describes the successful result as narrowly as possible. `any` never
  justifies a switch between null, array and error. An empty result keeps its
  container mode. [Sys_Func §7]
- **D7.6.2 — Raw helpers are only for adapters.** A raw helper that returns
  `-1` is only for adapters. A public wrapper checks error operands first,
  then changes negative results to `null`. No Lambda-surface path calls a raw
  helper. The registration checklist of Sys_Func §8 applies to every new sys
  func. The COW write-back value and the expression result are different
  concerns. [Sys_Func §7–§8]

## D8 Compilation and Execution Tiers

### D8.1 Front ends

- **D8.1.1 — First-party C parsers are the production front ends.** Lambda
  and LambdaJS each use a first-party C lexer and a hybrid
  recursive-descent/Pratt parser. Each parser reduces directly into the
  retained AST; no CST or second syntax tree is kept. The Tree-sitter grammars
  are reference grammars, editor and binding grammars, and differential
  verifiers. `lambda/tree-sitter-lambda/grammar.js` is the best structural
  reference, but it does not cover every corner case. The result of neither
  parser is normative alone. A disagreement goes to manual review against the
  syntax rulings. Then fix the wrong side, and pin the ruling with a fixture.
  Never edit a generated `parser.c` or a generated build file. [CGP5v2; rules
  5, 7]

### D8.2 The unified AST

- **D8.2.1* — Language is a property of the compilation unit.** One leveled
  catalog of core nodes serves the host and the guests. At least 80% of any
  guest uses core nodes. Language belongs to the compilation unit, never to a
  node. Operator semantics dispatch through the language profile. A program
  can mix languages; a module cannot. [U1, U2, U4]
- **D8.2.2* — Keep syntax variance as fields; never rewrite the tree.**
  Variance uses these tiers, in order of preference: fields and flags, then
  clause chains, then node kinds for one language. A node can be promoted to
  core, never demoted. One node for each concept (loop, match, if, assign)
  carries form fields. There is no tree rewriting, so desugaring hazards cannot
  occur. `for` is its own core node, because pure iteration and procedural
  loops have different colors. Purity is a core flag. [U3, U19, U22, U14]
- **D8.2.3* — Extract the common core after convergence.** Extract common core
  code only when two working clients exist. The rooting fix for guests is a
  prerequisite inside `MirEmitter`. [U12/U21, K17]
- **D8.2.4* — One indexed compilation unit.** Dense, stable IDs name nodes,
  scopes, bindings, functions and classes. One child-enumeration contract
  builds the parent, owner, binding, use/def and graph indexes. Lowering uses
  resolved identities, and it never repairs a binding. A core pass never adds
  a private walk of core children. Derived index facts are immutable. An AST
  extension must rebuild them before use. [U27, U28]
- **D8.2.5* — One typed pass manager; facts live on the AST.** One pass
  manager owns the schedule from parse to link. Passes declare the facts that
  they need and produce. The compiler records the time of each pass. Source
  contracts and core runtime facts live on the AST: the type of a node is its
  effective type, and a declared annotation is on the declaring node. Inferred
  types and constants are essential facts, not erasable ones. Erasable
  optimization facts hang off a side record of the type. There is no per-node
  fact table keyed by ID. Language profiles answer typed semantic questions;
  they do not own other pass schedules. [U29, U30v2]
- **D8.2.6* — Lowering is driven by demand.** Core expression lowering returns
  the full `MirValue` (D2.3.4). A demand (discard, any, required
  representation, destination register or branch) can avoid materialization.
  It never changes semantics. An unsupported demand falls back to generic
  lowering. Carrier conversion, semantic coercion and rooting keep their
  owners (D2.3.5, D6.3.4). PDF output demand carries an explicit destination
  and a checked status (D3.5.1). No ambient output context is installed. [U31,
  U32; PDF_Output §5.5]

### D8.3 Lambda execution tiers

- **D8.3.1* — The AST interpreter is the baseline tier.** The boxed
  AST-walking interpreter (T0) is the baseline. The retained AST is the
  runtime source of truth. The default policy is AUTO: each definition starts
  in T0, and a hot eligible definition can be promoted to a boxed MIR image.
  The tier selector has three modes: `interp` (T0 only), `auto`, and `jit`
  (eager whole-module MIR Direct, T1). In `interp` mode, an unsupported script
  or import fails before execution, with a diagnostic. A fallback to
  whole-module MIR belongs only to AUTO. [AI1–AI24, CGP1, CGP2]
- **D8.3.2* — Only hit counters trigger promotion; a handoff is one way.**
  Call counts, self-tail edges and loop back edges are counted at each site. A
  definition is promoted only when a counter reaches its threshold. The color
  of a definition never excludes it: T0 runs suspending definitions on their
  task activations. A running T0 activation can hand off to compiled code at a
  validated self-tail call or at a loop-head test. The compiled code completes
  the activation, and control never returns to the interpreter during that
  activation. A loop is eligible only if every live value at its head is a
  named frame slot, or is the state of a plain `for` walk. Threshold values
  are tuning, not design. [AI23, AI25, RA11, RA12]
- **D8.3.3* — Promoted code enters through the boxed wrapper.** A promoted
  image is always entered through its `_b` wrapper. The wrapper admits the
  arguments under the declared contracts, as the eager tier does. An `any`
  parameter is safe, because it has no raw carrier. A `var` parameter crosses
  the tier boundary through the caller-owned home transport (D5.4.6). An image
  is a cluster: the target plus the reachable promotable definitions, within a
  bound. Calls in the cluster are direct native edges. A body that the image
  cannot represent stays in T0. [AI ledger; CW33]
- **D8.3.4* — Background compilation publishes only at safe points.** A hot
  definition queues work for a bounded compiler pool that the process shares.
  The triggering call and the calls before publication stay in T0. A worker
  compiles from frozen AST and source facts into a private MIR context. It
  never changes shared script, module, constant or function state. Only a
  later execution safe point (a function entry or a loop-head test) verifies
  the generation and publishes the entries. A failed or stale image is
  discarded, and the definition stays in T0. Publication replaces no active
  frame. [D1.8, D6.4.3, D7.4.1, D8.5.1]
- **D8.3.5 — Static import prebuild is a shared keyed-future service.** One
  process-wide service owns a bounded pool and one task for each canonical
  module path and language profile. Discovery submits child requests, and it
  does not wait for the whole closure. A duplicate request gets the same
  future. A completed child releases only its direct parents. No worker waits
  for a child. An importer waits only for the future of the module that it
  uses next. A worker publishes only a retained AST template through the
  script cache; it never instantiates a module or lowers MIR. The receiving
  runtime plans T0 itself. The scheduler is common infrastructure, not a
  shared parser or evaluator. [D8.5.1]
- **D8.3.6 — The REPL is one persistent T0 session.** Each completed entry is
  parsed, built, planned and executed as a fragment against the retained
  script, scope and module slab. A rejected or failed entry is rolled back as
  a whole. Replay of the whole history is retired. In a session, the tier
  selector governs only promoted images. An entry outside the T0 surface is
  rejected with a diagnostic; it is never compiled as a separate MIR module. An
  interrupt raises a fault through the recovery frame, and the session
  restores its snapshot. [RI1–RI6]

### D8.4 LambdaJS execution tiers

- **D8.4.1* — One runtime, separate semantic walkers.** A retained `JsScript`
  owns its AST, its source facts and its module identity. It runs in the same
  `EvalContext`, GC heap, event loop and module registry as Lambda. Lambda and
  JavaScript keep separate semantic walkers and activation records. AST
  functions are a function body kind behind the normal call and construct
  capabilities, not a second dispatcher. Lexical environments are records that
  are traced precisely, and closures capture their mutable cells by reference.
  [JSI1–JSI13]
- **D8.4.2* — AUTO starts in the AST; MVP-Lmd is the MIR engine.**
  JavaScript defaults to AUTO. A unit that the interpreter supports starts in
  the AST interpreter. An unsupported unit uses whole-unit MIR. Hot
  definitions and loops can be promoted. MVP-Lmd is the only JavaScript MIR
  engine, for an explicit `mir` selection, the AUTO fallback and promotion.
  The old MIR engine is not a fallback. A forced `ast` selection fails closed.
  A selected MIR unit is not redirected to the AST, and a compile failure is
  reported without replay. A unit is classified before its declarations or
  user code run. [JSI16v2, JSI35, JSI36]
- **D8.4.3 — Dynamic source always runs in the AST interpreter.** `eval`, the
  `Function` constructor family, string timer handlers and `$262.evalScript`
  parse into a retained `JsScript` and run in the AST interpreter. This is
  true for every caller tier, and MIR never lowers dynamic source. A direct
  eval links to the live environment chain of the caller frame; nothing is
  projected or written back. Both tiers use the same binding resolver and
  declaration instantiation. Early errors use the bound AST facts, never
  source-text scans. This is a language boundary, not a fallback. [JSI ledger]
- **D8.4.4 — All tiers share one JavaScript semantic contract.** The AST,
  AUTO, dynamic-source and MIR paths share the MVP realm and the JavaScript
  operation contract. The execution policy selects the walker, the
  compilation and the promotion. It never selects different property,
  coercion, array, class or call semantics. Native adapters keep explicit ABI
  capabilities. Builds without MIR keep dynamic source, classes, realms and
  AST module loading. Observable bindings keep live environment cells.
  Per-site mutable caches stay forbidden (D7.4.1). JavaScript coercion and
  mutation stay different from those of Lambda (S1.11). A conformance failure
  is an implementation gap. It never justifies a change of tests, expectations
  or the baseline. [JSI ledger]
- **D8.4.5* — JS promotion has closed admission and keeps callable
  identity.** A JS definition compiles to MIR only on a hot hit: a call count
  or a loop back-edge count. A running AST activation can hand off at a
  loop-head test, in the same one-way shape as Lambda (D8.3.2). Admission is
  closed: a definition is eligible only if a checked body-and-dependency proof
  covers it. Every other definition stays in the AST interpreter. A promoted
  image keeps the identity, source, name, length, receiver, home and
  constructibility of the original callable. It never bridges an existing AST
  environment, and it never executes module source again. The admitted shapes
  and the thresholds are implementation details. [JS_Interpreter]
- **D8.4.6 — JavaScript modules use the shared module registry.** CommonJS
  `require()` and ES modules use the runtime module registry, not a private JS
  cache. A module publishes its namespace placeholder first, then instantiates
  its hoisted functions, then follows its static dependencies and executes.
  Import reads resolve on every read, so named bindings stay live. A `.ls`
  target compiles into the same registry, and its functions cross the normal
  boxed call ABI. One page has one `EvalContext`, heap, event loop, module
  registry and module-state authority. [JSI ledger]

### D8.5 Caches

- **D8.5.1* — The L1 script cache is in-process.** One persistent in-process
  cache holds executable script sources: main scripts, imports, harnesses,
  documents and guest scripts. The source identity is the exact bytes, the
  canonical identity, the source kind, the language profile and the
  resolution policy. AST and MIR artifacts are subordinate entries. A hit
  recreates the execution, heap, DOM, module and exception state. An optional
  byte limit evicts only inactive entries that have no dependents, least
  recently used first. A live lease can go above the limit. A recovery that
  closes an unfinished build claim wakes the waiters and poisons that key.
  Prebuild (D8.3.5) and background compilation (D8.3.4) are producers with
  their own keys. Admission always checks the exact source identity. The cache
  stays in-process and source-based. [MC1, MC2, Script_Cache]
- **D8.5.2* — A disk cache stores a code image with a relocation journal.** A
  C-source cache is rejected, because it makes a C toolchain a production
  dependency. A MIR-bitcode cache is rejected, because it caches the wrong
  phase. The prerequisite is a lowering without baked pointers: a baked
  pointer has no relocation record. The first load model generates the front
  end again and loads the back end. [MC3, MC5/MC6, L3-1..L3-2]
- **D8.5.3* — Every cache entry is born verified.** A differential write
  verifier is mandatory, and it fails closed: compile twice at different
  addresses and compare. A byte that the journal cannot explain stops the
  caching of that module. Keys include the build stamp, architecture,
  optimization policy, format version and transitive source hashes. Artifacts
  are local, hash-checked, size-limited, never signed and never distributed. A
  per-instantiation mutable cell must become a named data item before its
  language joins. [L3-6..L3-9, MC7/MC8]

### D8.6 Emission testing

- **D8.6.1 — The emission ratchet has 0% slack.** An increase in emission size
  above `test/mir/mir_budgets.json` fails the baseline. Growth needs a manual
  budget change in the same commit; the budget diff is the review artifact. A
  decrease tightens the budget automatically. The test never edits its own
  configuration. Remove a nondeterministic probe; do not pad it. There is no
  hidden platform slack. [MT7]
- **D8.6.2 — One artifact contract.** The MIR dump path works in release
  builds. `--no-log` is the master switch, and mandatory emitter aborts stay
  live under it. `mir-check` sidecars assert names and instruction shapes over
  the finalized dump, never raw immediates. [MT1, MT2]
- **D8.6.3 — Dynamic oracles verify liveness.** Forced-GC runs and poison
  sweeps verify liveness. A stressed run must match the unstressed run byte
  for byte. The baseline gates these runs. A static liveness duplicate is
  rejected. An independent structural verifier waits until emission is final.
  [MT4, MT3]
- **D8.6.4* — The unified-AST consolidation has hard exit ratchets.** The
  consolidation finishes only when three ratchets pass, and they fail closed:
  - at least 2,000 net lines removed from the runtime and JS compiler scope;
  - at least 10% less Lambda compile time;
  - at least 20% less JS compile time.

  Finalized MIR volume is a required numeric diagnostic, not a ratchet.
  Measurements use fixed release manifests and repeated runs. Cache hits, code
  moved out of scope, formatting and comment removal cannot satisfy a ratchet.
  [U33–U36]

## D9 Build, Modules and Packages

*Terminology.* A Lambda script module is a **package**. A Jube native DSO is a
**module**. Context makes the meaning clear where the text uses "module"
loosely.

### D9.1 Build layering

- **D9.1.1v4* — Input, Radiant and Lambda have separate owners.**
  `lambda-input` owns shared libraries, data, parsers, native formatters and Mark APIs.
  Lambda owns script-based writers and their execution (D9.3.4).
  Radiant owns native event collection, layout, rendering and the state store.
  Lambda owns event handling and uses Radiant services through host interfaces.
  Radiant depends on `lambda-input`, not on the Lambda runtime. Lambda
  integrates the runtime and Radiant. Input and Radiant ship as static
  libraries. Dynamic libraries remain for Jube modules. Optional host
  services cross explicit interfaces. A missing GC service never falls back
  to weak, null or malloc allocation. [SM1–SM3, SM6; Radiant_Split §2–§3]
- **D9.1.2v2* — The input service owns resource acquisition.** `lambda-input`
  owns source acquisition, source identity and neutral source caches.
  Radiant requests resources through that service. Lambda owns execution,
  script and MIR artifacts and runtime-coupled scheduling. Radiant owns
  rendering artifacts. Host policy governs resource access (D11.1.2).
  [SM12; Radiant_Split §4.1]
- **D9.1.3v2* — Provider ownership beats include ownership.** The owner of a
  public declaration, or a dependency of that owner, defines its provider.
  Include and whole-archive link checks enforce project boundaries.
  Input and Radiant have no runtime imports. [SM13, SM5; Radiant_Split §6]
- **D9.1.4v2* — The input project owns the Mark API.** `MarkReader` reads the
  shared data model. `MarkBuilder` and `MarkEditor` use the `Input` owner.
  Ownership resolves layering; it does not require callback abstractions.
  [SM §9.4; Radiant_Split §3.1]
- **D9.1.5* — GNU Make and Premake define the project build pipeline.** GNU
  Make is the build entry point and executor for Lambda, `lambda-input` and
  Radiant. Premake reads each owner's JSON build configuration and generates
  Makefiles where needed. Project and dependency builds use no other build
  system, including CMake, Meson, Ninja or Autotools (Autoconf/Automake).
  [Radiant_Split §6]

### D9.2 Build profiles

- **D9.2.1v2* — Headless has three profiles.** Profile A (`lambda-cli`, D9.2.2)
  omits `radiant` at link time. Profile B is a runtime flag inside the full
  `lambda.exe`. Profile C (`lambda-headless`) is the full engine with a null
  windowing backend in place of the GUI, OpenGL and webview toolkits; it
  refuses `view` and `edit` explicitly. B and C compile the same Radiant
  sources, with no `#ifdef` removal of display code. Lambda supplies optional
  scripting services through the Radiant host interface. Public headers never
  define build-profile macros. [SM9, SM10, SM16; Radiant_Split §6]
- **D9.2.2 — `lambda-cli` is the runtime-only host.** It has only the Lambda
  runtime: core, io, the engine, the validator and their libraries. It
  excludes Radiant, the JS/TS runtime and DOM, the Jube host modules and the
  HTTP server. It is a reduced build profile, not a bundle (D1.1). An excluded
  command fails with an explicit diagnostic. An import of a JS or Jube module
  is an ordinary import error. Nothing degrades silently. [SM15]
- **D9.2.3 — `lambda-wasm` is the browser evaluation profile.** It is smaller
  than `lambda-cli`. It has the parser, the AST interpreter, the core values,
  precise GC and a stateful in-memory REPL over source and data that the host
  supplies. It excludes MIR and all JIT machinery, the network, the
  filesystem, ambient system access, threads, task scheduling and the large
  format and package families. The Static Modules record has the exact
  manifest. Evaluation is synchronous, and it stays in the AST. An unavailable
  capability fails explicitly. Retained values and operations keep their
  ordinary semantics and ownership. It is a build profile, not a Jube bundle
  or a WASM guest language. [SM17, SM §13.4]

### D9.3 Script packages

- **D9.3.1 — A package is the compilation unit.** A Lambda script module (a
  package) is the compilation unit. Its language profile is a property of the
  unit (D8.2.1). Its module-scope bindings are immutable; scope guards enforce
  this structurally, and guests are exempt. Its instantiation has effects,
  although its bindings are immutable. Package state lives in per-context
  slabs (D6.4.3). [RG14, U2]
- **D9.3.2 — Imports bind boxed entries; initialization is a transaction.** An
  import resolves to the boxed `_b` entry of an exported function, so an
  export must have a boxed entry (D7.3.4). Package initialization is a
  transaction barrier. A failed initialization rolls back. A module with
  half-built top-level state never becomes importable (S7.7.6). [DF15, ER-D2]
- **D9.3.3 — Packages are cached in-process and distributed as source.**
  Imported packages use the in-process cache (D8.5.1). They are distributed as
  source (D1.7). Compiled artifacts are local caches only.
- **D9.3.4 — `lambda.*` is the one root for shipped code.** The root name is
  reserved (S16.10.1v2). It has three tiers:
  - `lambda.sys.*`: the system functions, which the prelude imports
    unqualified (S17.2.1);
  - `lambda.<module>`: built-in modules; the bare name is an alias, so
    `import math` is the same as `import lambda.math`;
  - `lambda.<package>`: shipped script packages. Document packages group
    under `lambda.doc.*`.

  One path names one thing: no path is both a built-in module and a package.
  `lambda.io.<leaf>` can name a built-in module whose functions park the
  calling task on IO, which a Jube module cannot do (D9.5.2). Such a module is
  imported only by its full path. Shipped paths map to the source tree under
  `<lambda-home>/package/`, one component per level, with no exceptions.
  [S17.2.1, S17.2.2, S16.10.1v2; Demo_Mail ML11]
- **D9.3.5 — One import resolver serves every caller.** Import resolution
  follows the S16.9.8 roots. A package path is looked up by name only: the
  built-in modules, then the runtime-registered host modules, then the shipped
  packages. Any other root fails with E216. That root space is reserved for the
  package registry, and it is never tried against the working directory. A
  relative path resolves against the directory of the importing script. Thus a
  script with relative imports runs in place, not from a copy. [S16.9.8]

### D9.4 Jube modules

- **D9.4.1 — The core is closed; everything else is a module.** The core is
  ECMA-262, the engine substrate, `./lib` and a closed allowlist of globals
  (console, timers, `queueMicrotask`). Everything else is a Jube module. A
  change to the allowlist is an ADR-level change. The core keeps file IO and
  the shared stream core for performance. Spec-surface stream and crypto APIs
  are modules. [JA1]
- **D9.4.2 — Modules are verified, negotiated and transactional.** A module is
  a DSO and a `module.json` manifest. Its hash is verified, and its ABI and
  host API are negotiated before any module code runs. Modules load lazily and
  register as a transaction: a failed initialization rolls back. A module
  lives for the process; there is no hot unload. Evolution is additive only,
  behind size gates. Static registration is only for development, and it must
  behave the same. [JA2, JA5, JA11]
- **D9.4.3 — There is one strict host-API tier.** Every module uses only
  `jube.h`. In-tree modules get no softer tier; they are the constant test of
  the contract that third parties get. Modules import no host-internal
  symbols. The API surface comes from real use, not speculation. Signatures
  are Lambda type-syntax strings with a mandatory `fn`/`pn` keyword (D7.1.1).
  [JA5, NM §6.2, §6.4]
- **D9.4.4 — No module links another.** The core never links modules. A module
  never links the symbols of another module; it uses manifest dependencies or
  a script re-export. The manifest can be read without `dlopen`. It mirrors
  the C descriptor table, which is the ground truth. [JA8, NM §7]
- **D9.4.5 — Every module kind names its conformance gate.** Each module kind
  has a gate in CI. The core uses Test262. Web-platform modules use their WPT
  slice. Node-compat modules use the node baseline. Languages use their own
  corpus. Data packs use the pack corpus and integrity checks. [JA9]
- **D9.4.6* — The host exports only an allowlist.** Host code compiles with
  hidden visibility by default. The dynamic export table of the host is
  exactly the checked-in allowlist; a loaded module can resolve only those
  symbols. A module that needs another symbol gets it case by case: an
  allowlist entry that names its users, and an API marker on the declaration.
  An entry that no module imports is removed. A host without the Jube loader
  exports nothing. [JA5.1]

### D9.5 Jube data and async contracts

- **D9.5.1 — Items cross; host structs use virtual carriers.** `Item` is the
  only value currency (D1.2). Native structs cross as virtual carriers
  (D9.5.5). An engine-owned object with internal slots can use the same
  carrier when its native slots are primary. The representation selects the
  carrier, not the module ownership. Shape-backed ECMAScript objects stay
  typed maps. The vtable owns the brand, the precise trace and the finalizer.
  Raw C pointers are never visible to a script. System resources are integer
  resource IDs. [JA6, NM §6.3, §8; JR7]
- **D9.5.2 — Modules are shielded from the async substrate.** There is no
  escape hatch to the event loop. A micro-kernel of five concepts serves
  modules: scheduling, a thread-safe completion post, a blocking pool, the
  resource table and shutdown. Completions arrive only on the JS thread, in
  loop order. Modules never pump, poll or drain. A task never parks while a
  host-API frame is on its stack. The call from the host into script raises a
  native barrier, and a suspension below that barrier is a reported fault. [JA7,
  Concurrency §4.4, RA10]
- **D9.5.3* — Guest hosting crosses no core types.** No `Runtime*`,
  `EvalContext*`, `Input*`, `MIR_context_t` or `AstNode*` crosses the
  boundary. Only opaque handles and versioned sub-API tables cross. Every
  callable runtime helper has catalog metadata: signatures, ownership, GC and
  allocation effects, re-entry and exception behavior. Lowering resolves
  through the catalog, never by a guess. The host owns heap activation,
  context install, side-stack entry and exit, and recovery boundaries. The JS
  error lane (D7.4.3) is not reused as the exception model of another guest.
  [Lang_Hosting §5–§7, §13]
- **D9.5.4 — Declared interfaces are the only host-object protocol.** The
  surface of a host type is its interface declaration in Lambda type syntax,
  compiled to member records. Record-owned hooks for open names and indexes,
  and a destroy callback, complete it. There is no fallback tier; the
  type-level dispatch fallbacks are retired. Property keys cross the member
  ABI as Items (string, symbol or number). They resolve through one borrowed
  byte view, and they are never materialized again. [DOM4 D4k, D4j]
- **D9.5.5* — Three virtual carriers mirror the container taxonomy.** The
  carriers are `vmap`, `varray` and `velmt`. Each one occupies its structural
  TypeId band: VArray directly after Array, and Velmt directly after Element.
  The sequence-family tags form one closed numeric band. Static asserts
  enforce this order as an Item ABI contract. A physical storage decode still
  needs an exact guard (D2.3.3). Host data crosses as the carrier that matches
  its shape. A virtual array reads live through its vtable. A virtual element
  has both its list face and its map face. Member records (D9.5.4) are the
  named-member protocol on all carriers. Do not flatten element-shaped data
  into a map, and do not copy array-shaped state into real arrays. [DOM4 D4l]
- **D9.5.6v2* — Lambda and Radiant meet through explicit contracts.** Lambda
  hosts the standalone Radiant library through native engine and host-service
  interfaces. Scripting adapters and the `radiant-dom` module belong to Lambda.
  The runtime core does not require Radiant. Shipped document layouts connect
  automatically to pure package `layout(parent, children, ctx)` exports.
  [JA15, SM10; Radiant_Split §4; Radiant_Design_Custom_Layout §6]

## D10 Documents and Shipped Packages

### D10.1 Document formats

- **D10.1.1 — `.mark` is the one Mark file extension.** New files, examples,
  generated artifacts and file associations use lowercase `.mark`. Filename
  detection of Mark recognizes only this extension, with no case sensitivity.
  Other short forms are not aliases, also for legacy input. Spell the name of
  the format; extra extensions fragment the tools and add no capability.
  [Input_System §Mark file extensions]
- **D10.1.2 — An explicit format overrides the filename.** When the caller
  selects `mark`, Mark input accepts any filename, with or without an
  extension. Thus filename aliases are not necessary. [Input_System §Mark file
  extensions]
- **D10.1.3 — A domain document in Mark syntax has its own format.** A domain
  document that uses Mark syntax has its own format name and extension; it is
  not a Mark alias. For example, `.slides` is a slide deck with a
  `<presentation>` root, read as the format `slides`. Its own route selects
  the format explicitly. Filename detection of Mark still recognizes only
  `.mark`. The extension names what the document is; the syntax is shared.
  [Input_System §Mark file extensions; Lambda_Pkg_Slide_Presentation §0]
- **D10.1.4 — XHTML uses XML syntax and gives the HTML element tree.** The
  format `xhtml`, the `.xhtml` and `.xht` extensions and the MIME type
  `application/xhtml+xml` select the strict XML parser with namespace
  validation. The result uses the document, doctype and comment
  representations of the HTML parser. Prefixes resolve to local names, and the
  namespace identity is kept. Name case is kept (S8.2.2v6). Radiant shows the
  result in standards mode. Known public identifiers select the built-in
  entity catalog; no DTD is fetched. Other doctypes admit only the five XML
  entities. [Input_XML §XHTML admission]
- **D10.1.5 — SVG always uses HTML5 SVG parsing.** SVG in `text/html` follows
  the WHATWG HTML tokenizer and the foreign-content tree construction.
  External SVG (standalone files, image resources and external references)
  uses the same parser, and it gives the same namespace-aware element subtree.
  There is no separate XML-only SVG intake. Explicit XML and XHTML input do not
  change. [Radiant_SVG2 §SVG parsing admission]
- **D10.1.6 — SVG export serializes the current DOM.** SVG source export
  writes the current SVG DOM, edits included, through the shared
  namespace-aware serializer. The export of a rendered page to SVG or PDF is a
  separate paint contract. [Radiant_SVG2 §DOM export and virtual-frame
  snapshots]
- **D10.1.7* — An animation snapshot uses the virtual-timer frame.** Before
  export, settle the DOM updates and the presentation state of the frame that
  the virtual timer selected. Capture does not move virtual time. The snapshot
  keeps that frame when it is opened again. A repeated capture without edits
  or time movement gives the same state. Export during a wall-clock animation
  is not covered. [Radiant_SVG2 §DOM export and virtual-frame snapshots]

### D10.2 Document packages

- **D10.2.1* — The DOM behavior package owns the editing policy.** The full
  user-agent editing policy belongs to the shipped Lambda DOM behavior
  package, never to Radiant native code. Radiant owns the input gate and the
  standard DOM mechanisms: editing-host recognition, events, Selection and
  Range, clipboard and composition transport, observation, geometry and
  checked DOM mutation. The package owns the default actions, normalization,
  history, `designMode` and the full `execCommand`/`queryCommand*` surface.
  User input and legacy commands use one operation model. The gates are the
  WPT editing suites and the pinned Chromium editing corpus.
  [Radiant_Design_Editable §20]
- **D10.2.2* — LaTeX is read by TeX expansion, never by TeX typesetting.**
  Every LaTeX parse runs the Lambda TeX expansion engine at the input boundary.
  Then one digester builds the Mark AST. Thus one source gives one AST at
  every entry point. The engine executes TeX programming: catcodes, macros,
  registers, groups, conditionals, e-TeX and expl3. Document constructs go to
  the `lambda.doc.latex` adapters, which own their meaning. Bundled packages
  are unchanged upstream programming packages or Lambda-owned bindings. The
  engine never reproduces the TeX paragraph builder, page builder, output
  routine or font machinery; Radiant owns layout. TeX source never becomes
  Lambda code (S1.8). Reads are read-only, and they are limited to the
  document directory and the bundled resources. The output depends only on
  the source and the resources (S12.1.1v2). A package or class name resolves
  once, in a fixed order: bundled package, adapter, local file, then a
  diagnostic. [TeX_Engine §1–§8]
- **D10.2.3* — Document math keeps the layout policy of its notation.**
  `lambda.doc.math` exports Presentation MathML, and it accepts MathML trees
  beside TeX and AsciiMath. TeX-derived layout follows TeX. MathML-derived
  layout follows MathML Core. Both share font facts and box geometry. MathML
  is never converted through TeX. The package owns the conversion policy; XML
  syntax stays an IO mechanism. Unsupported features report errors, and the
  support is a documented subset. [Lambda_Pkg_Math §11]
- **D10.2.4 — Bundled fonts are shared resources.** Generated HTML and SVG
  `@font-face` rules reference the bundled fonts by URL. They do not embed the
  font bytes in each document. Font bytes that a caller supplies without a URL
  keep their embedding. [Lambda_Pkg_Math §8]

## D11 Security and Capabilities

*Authority comes only from the trusted host and the user. Code never grants
authority to itself.*

### D11.1 Trust and IO

- **D11.1.1* — There are three trust tiers.** T1 (system) is an in-process
  DSO, and it is not a security boundary: to load one is the same as to link
  it into the host. T2 (confined) is a separate process with an OS sandbox and
  a capability broker; all three parts are necessary. T3 (sandboxed WASM) is
  safe by construction. It is the recommended default for untrusted code.
  Third-party native modules never run in the runtime process. T1 admission
  needs a declaration of thread-isolation safety; without it, only process
  isolation is permitted. [JA10]
- **D11.1.2* — One central IO API is the only IO door.** Lambda, Radiant and
  all modules use one IO API. Raw IO stays in `./lib`. The API carries policy:
  path and capability gates, isolation, audit and quotas. It is a real
  boundary for scripts and a governance point for T1. Thus a checker can find
  any direct `open()`. [JA16]

### D11.2 Apps and scripts

- **D11.2.1* — Apps and scripts run inside an admitted root.** An app or an
  ad-hoc script can read and import inside its admitted source/resource root,
  and it can load approved shipped packages. A dependency outside the root
  needs an additional grant. A loose file first grants only itself and the
  approved shipped libraries. The selection of a project or folder sets a
  wider root. Transitive imports, initializer IO and cache hits obey the same
  policy. To import an API or to resolve a path grants no authority (S2.4.4,
  S16.9.8). Imported code shares the grants of its caller and never expands
  them. Loading has no unrestricted bootstrap phase. [App_Safety §4.1]
- **D11.2.2* — Create freely; overwrite only with approval.** New files under
  the app or project root are permitted by default. To overwrite an existing
  file needs the approval of the user. Creation is an atomic create-if-absent,
  never a check followed by a truncation. Modification, append and replacement
  cannot avoid approval through other APIs or through delete-and-recreate. A
  remembered overwrite grant needs explicit consent. Protected host and OS
  resources stay excluded. Root mutations go through a broker, so native code
  cannot avoid the distinction. [App_Safety §7.3]
- **D11.2.3* — The trusted host owns the grant settings.** Grants for each app
  and each ad-hoc script live under `~/.lambda/setting`, and the trusted host
  controls them. Apps and user scripts cannot create, change, delete, replace
  or redirect this security state, also not through ancestor grants or path
  aliases. The host binds records to admitted principals, and it keeps their
  expiry and revocation. OS confinement and broker checks enforce the
  protection; filesystem ownership by the same user is not sufficient.
  [App_Safety §6.4]
- **D11.2.4* — Network access is denied by default; updates are a host
  service.** App and script network access is denied by default. Package
  auto-update is a separate pull service that the host owns. It polls and
  downloads verified updates only from admitted package endpoints, and it gives
  no network authority to package code. Requests carry no user data,
  credentials or telemetry by default. The user can pause, disable and restore
  updates, and that state is protected (D11.2.3). A global offline setting
  overrides this exception. An update cannot expand app grants, and it cannot
  replace user project files without approval (D11.2.2). [App_Safety §8.3]
- **D11.2.5* — Uninstall keeps private app data.** To delete retained
  persistent data needs a separate, explicit action by a trusted user. Cache
  and temporary files can be discarded. The removal of installation or grant
  state alone does not authorize the erasure of persistent data.
  [App_Safety §7.1]

### D11.3 Media capabilities

- **D11.3.1* — Audible output depends on the document origin.** The start of
  audible output (synthesized music, decoded audio, speech and unmuted media)
  is judged for each requesting document. A document whose top-level source is
  a local file that the user opened can start audio from any `pn` context. A
  document from the network can start audio only after sticky activation.
  Activation comes from a trusted input that the host delivers: a pointer
  press, a touch end or a non-modifier key press. The document keeps
  activation for its life. Events that a script dispatches never activate. A
  refused start fails visibly. Muted playback and output-free work need no
  activation. Each document is judged by its own origin. Only the host can
  grant activation in advance; a document never grants it to itself.
  [Radiant_Design_Speech §5]
- **D11.3.2* — The microphone needs explicit acquisition and separate
  grants.** Acquisition is procedural, through the existing IO and host
  capability route. It needs both the OS permission and a grant for the
  requesting document. Pure evaluation and scene descriptions cannot open a
  microphone. Output activation (D11.3.1) grants output only. The trusted host
  owns the permission UI and the grants; a document cannot grant itself access
  or serialize a permission into a scene. Capture uses bounded native storage
  and copied buffers at the host boundary. An explicit stop, document
  teardown, revocation or device loss closes the capture owner. Denied and
  unavailable states are visible. Monitoring is off by default, and it obeys
  D11.3.1 when it is on. [Scene3D §9.3]

---

## Appendix A — Implementation Status

Status of the `*`-marked rulings on 2026-11-10. Each entry gives only the
status and the live residue. The detailed footnotes, evidence and dates are in
the [implementation history §2](../vibe/impl/Lambda_Impl_Formal_Design_History.md#2-implementation-footnotes).

| Ruling | Status |
|---|---|
| D1.2, D1.3, D1.5 | Partial. The production LambdaJS path uses the shared `Item` and precise-GC substrate. No private guest runtime is active since 2026-10-07; a future one must meet these contracts. |
| D1.4 | Partial. Explicit completions are in place for Lambda, LambdaJS and Jube (D7.4.3). No private guest ABI is active. |
| D2.1.5 | Partial. Raw `>> 56` sites, open-coded double reads and raw `MIR_EQ` emissions remain. |
| D2.3.3 | Met by admission at the `_b` boundary, not as written: the unboxing helpers are unchecked casts. The ruling text waits for a user decision. |
| D2.3.4, D2.3.5 | First slice in place (2026-08-28). Migration of the remaining lowering sites continues. |
| D2.4.1 | First slice in place. `f16?`/`f32?`, mutable ArrayNum views and vector kernels are pending. |
| D2.5.2, D2.5.3 | Core in place (lane gating 2026-09-27; continued initializer outcomes 2026-11-02). Aggregate package and demo qualification is in progress. |
| D3.1.1 | Layout (map → array → element) and the nominal descriptor are in place, apart from open instances. The header cache bit and the retirement of the object TypeId are not complete. |
| D3.1.2 | Not implemented. |
| D3.1.3 | No identity carrier exists (DO25). |
| D3.1.4 | Bound `fn` methods are in place. The compile-time rejection of a bare `pn` method reference is not verified on both tiers. |
| D3.1.5 | In place for the MVP-Lmd profile (2026-10-07). The full engine still allocates plain maps without a nominal record. |
| D3.2.2 | The representation-sensitive ArrayNum `==` is a known live bug. |
| D3.4.4 | Reads conform. Byte confirmation for transition-tree edges is not built. |
| D3.5.1 | In place for both tiers with explicit pure-result demand. Producer-return lists and retained children stay open (DO32). |
| D4.1.1 | The kind-discriminated `Type*` layout has no design record (DO22). |
| D4.2.3, D4.2.4 | Continued outcomes in place (2026-11-02). Constrained types enforce only their base; the `is`/runtime/validator divergence is open. |
| D4.2.5, D4.2.6 | Partial. Typed path stores need no shape compare below a declared root. The certificate guard still runs for each access; `bool[]` has no native lane. |
| D4.4.1, D4.4.2 | The native profile is in place (2026-10-15). Buffer-counted arrays, offset references and cyclic schemas are separate extensions. |
| D5.1.4 | In place (2026-10-02): arena retention, mark/rewind and single-owner scratch arenas. |
| D5.1.5 | The arena-backed builder with tail growth does not exist yet. |
| D5.2.3 | Reference counting is not implemented. |
| D5.2.4 | Release default off and the registry-free pool hot path are in place. Counter-only statistics and the `stack_alloc` split are pending. |
| D5.4.3 | Stage 1 and place-home publication are in place. Other Stage 2 checks are pending. |
| D5.4.4 | Partial: straight-line handles and the path walk are in place. Synthesized handles and carried facts are not. |
| D5.5.2 | Partial: the ownership lattice has no GC → pool edge. The full audit is pending. |
| D5.7.1, D5.7.2 | Draft design; no phase has started. |
| D6.2.1 | In place for Lambda and LambdaJS (companion-lane ABI only; unified outcome metadata 2026-11-03). Removal of the ownerless-slot GC fallback is gated on its inventory. |
| D6.2.3 | Lambda helper results need no re-home. LambdaJS keeps the conservative re-home path. The back-edge reclaim proof is open (DO24). |
| D7.1.2, D7.2.3 | Confined parent-`fn` captures are qualified (2026-11-02). |
| D7.1.3 | `may_await` and a fail-closed `may_defect` are in place. |
| D7.3.1, D7.3.4 | Binder direct-edge variants and opt-in guard hoisting are in place. |
| D7.4.2 | In place; the trailing scalar-home operand is retired. |
| D7.4.3 | In place (2026-08-17) for Lambda, LambdaJS, Jube and hosted boundaries. |
| D7.5.2 | Not started: process isolation first, thread isolation after the audit and DO20. |
| D8.2.1–D8.2.6 | The shared indexed compiler substrate and pass manager are in place. Structural convergence of the two front ends is incomplete. |
| D8.3.1–D8.3.4 | AUTO, strict `interp`, loop and tail handoff, clusters and background compilation are in place. Handoff breadth stays open (DO33). |
| D8.4.1, D8.4.2, D8.4.5 | AST-first AUTO with MVP-Lmd MIR is in place. Promotion admission grows by checked extensions. |
| D8.5.1 | The common source registry and its loaders are in place. |
| D8.5.2, D8.5.3 | Not started. |
| D8.6.4 | Instrumentation is in place. The exit ratchets are not yet met. |
| D9.1.1–D9.1.4, D9.2.1, D9.5.6 | The three-project extraction has started. Input and Radiant still depend on runtime providers. See `vibe/impl/Lambda_Impl_Radiant_Split.md`. |
| D9.1.5 | Partial: input's dependency recipes use Premake and Make and were verified on macOS; Linux and Windows remain unverified. Lambda's standalone libpq recipe still invokes upstream `configure`. See `vibe/impl/Lambda_Impl_Formal_Design_History.md` §2 and `vibe/Lambda_Design_Radiant_Split.md` §6. |
| D9.4.6 | In place on macOS. Linux is generated but not verified. Windows is not covered. |
| D9.5.3 | Not active since guest-language hosting was removed (2026-10-07). |
| D9.5.5 | Partial: distinct VArray/Velmt TypeIds and carrier dispatch are in place. Some live collections are pending. |
| D10.1.7 | In place for the current inventory (2026-10-18). |
| D10.2.1 | In place (2026-09-07). |
| D10.2.2 | Partial: expansion is the only front end. The layout-metrics hook is pending. |
| D10.2.3 | Partial: export and strict input are in place. The MathML box builder is pending. |
| D11.1.1 | T1 verification layers are staged. T2 and T3 are not built. |
| D11.1.2 | Partial: the main clients are extracted. The remaining IO surface is incomplete. |
| D11.2.1–D11.2.4 | Components only (2026-11-02). |
| D11.2.5 | Partial: the experimental host keeps data. Uninstall integration is pending. |
| D11.3.1 | Partial: in place for Lambda rows on macOS. |
| D11.3.2 | Partial: in place for the macOS core profile. Physical permission and hot-plug qualification are pending. |

## Appendix B — Open Design Issues (DO#)

Each entry states the question and a pointer. The deliberation is in the
record. Closed entries (DO15, DO26, DO30) and the history of open entries are
in the [implementation history §3](../vibe/impl/Lambda_Impl_Formal_Design_History.md#3-open-issue-history).

**Values and containers**
- **DO1** Nullable-lane residue: boxed fallback for `any`, `number` and
  unions; observability of an absent versus a null-valued optional field; the
  scope of flow proofs; the native `pn` write-back ABI; ArrayNum
  re-promotion. [Nullable §10]
- **DO2** Scalar-home audits: result-bearing dispatch helpers, resume-slot
  reads, exception-peek rematerialization, the RetItem census and
  donated-home lifetime proofs. [SG §5]
- **DO3** Boxing: do union parameters stay boxed? Does the return type take
  part in unboxed-call selection? What is the policy for variadic functions?
- **DO4** `MirValue`: its size (measure first), and whether the planned
  occurrence source replaces the actual-rep analysis. No second analysis
  ledger. [Lane §9]
- **DO25** Node identity (D3.1.3, S5.1.4v2): where the identity lives, the
  universal ID form, which operations copy it, and whether `===` needs a fast
  path. [OB10; Design_Reference §4]
- **DO27** Instance type alteration (S2.1.5): can reconstruction reuse the old
  buffer, and how are extra fields carried? [OB18]
- **DO28** Tier carrier divergence for annotated module bindings: one carrier
  for each declared contract at the shared boundary would remove the guard.
  [D3.2.2, D4.2.1]
- **DO31** Can an object declaration also name an element tag, and how do
  the tags of a hierarchy relate? [D3.1.1]
- **DO32** Virtual PDF output: wider pure-producer demand, retained-child
  representation (SO48), remaining object encodings, native helpers and
  Radiant integration. [PDF_Output §5.6]

**Memory and GC**
- **DO5** Numeric GC fallback: bring the per-boundary inventory to zero, then
  delete the scalar heap. A donated persistent home needs an explicit owner
  and a borrowed-versus-retained ABI. [Stack_API §15.5–15.6]
- **DO6** Side-root slot metadata (boxed Item versus raw pointer), needed only
  for a moving collector; the cached-raw-copy idiom for hot native loops.
  [Rooting CQ6/CQ7]
- **DO7** Stack maps (kept in view): the only route to zero rooting cost in
  the mutator; they need a fork of the MIR backend. A moving or generational
  collector stays a non-goal.
- **DO8** COW counts: a saturating count waits for live-corpus counters. A
  chunked-children element for huge fan-out waits for the editor benchmark.
- **DO9** Memory Context stage 2, including MIR code-page bytes; `push` and
  `splice` on a module-level `let` bypass E211.
- **DO10** Stack-frame checks: concat without tail-payload rebase;
  temporaries live across a mid-expression `wait`; the layout split for
  precise closure-environment tracing.
- **DO24** Can a compiler-generated wide temporary be live across a loop back
  edge under the D6.2.3 reclaim? The emitter must prove the answer before the
  reclaim is gated on it. [RVO11, Return_Value §4a]

**Compilation and execution**
- **DO11** Dual functions: promotion-aware numeric inference, guard-failure
  counters, lazy generation of cold boxed bodies, and the "speculate int"
  revisit.
- **DO12** MIR cache: GC-root registry reset under L1, thunk identity under
  lazy generation, a growth bound for the name pool, and the L3 open items.
- **DO13** Emission testing: guest-transpiler scope, the MT6 coupling spike, a
  clean zero-root-elision fixture, and the missing MIR Direct sys-func
  specializations (pinned by a forbid fixture).
- **DO14** Unified AST: the "views" variance tier (adopt only when a shared
  pass needs it).
- **DO29** Typed `var` rebinding: arbitrary places, async rebinding, and a
  scalar-lane binding passed to an untyped `var` parameter. [COW §11.10]
- **DO33** Interpreter tier breadth (D8.3.2): satellite budgets under MT7,
  cross-context promotion cells, loops outside the eligible shape, a second
  handoff loop per definition, work-weighted counts, an image budget, and the
  call-site matrix for wider promotion. [Ast_Interpreter AIO1–AIO13]

**Runtime services and modules**
- **DO16** Name identity: the dynamic-intern growth bound, the pooled-flag
  audit, GOT rebuild on L1 hits, mapped-backing granularity, and the MIR-cache
  reconciliation (with DO17). [NI §8]
- **DO17** Const pool: decimal sharing, datetime packing, cross-module literal
  dedup, mapped-leaf page retention, the eager-materialization benchmark,
  serializer heuristics, compression and incremental append.
- **DO18** Sys-func registry: the public mutation convention, the public
  result of `splice`, and the confirmed ArrayNum carrier violations.
- **DO19** Jube: the globals allowlist audit, the WPT harness, the data-pack
  design, `dynamic_lookup` retirement, the Radiant-domain host API, the
  remaining IO policy details, the T2 interface and sandbox profiles, the JS
  semantic-adapter items, `JubeTypeDef` ownership and Windows parity.
- **DO20** Cross-isolate lifetime for shared graph Items (promote-on-share is
  recommended). Decide it before thread-mode workers. [Concurrency O-D]
- **DO21** Static modules: Class F scheduling, the profile-B headless audit,
  the runtime-native IO inventory and Windows export macros.

**Types and shapes**
- **DO22** First-class `Type*` representation needs its own design record:
  the kind-discriminated layout, the canonical order for union normalization,
  and the type-graph de-pointering census.
- **DO23** Shapes: cross-`Input` type sharing and shape serialization.

## Appendix C — Decision-Record Index

| Section | Records | Where argued |
|---|---|---|
| D1 | JA4/JA13, J5, P1–P10, LC/Lane/MT preambles, REH-D1–REH-D5, rules 5/7/14 | `Lambda_Design_Jube_Architecture.md`, `Lambda_Design_Jube_Lang_Hosting.md`, `Lambda_Design_Runtime_Error_Handling.md` |
| D2.1 | Item_Boxing §0–§8 (R7–R10, W1–W3) | `Lambda_Design_Item_Boxing.md` |
| D2.2 | Double_Boxing; Int_Type §5.1; Stack_API §15; JS_MVP_Lmd §9 | `Lambda_Type_Double_Boxing.md`, `Lambda_Semantics_Int_Type.md`, `Lambda_Semantics_Number_Model.md`, `Lambda_Design_Stack_API.md`, `jube/JS_MVP_Lmd.md` |
| D2.3 | DF1, DF2, DF4, DF15; Nullable §7.2; Lane §1–§9 | `Lambda_Design_Compiling_Dual_Func.md`, `Lambda_Design_Compiling_Nullable.md`, `Lambda_Design_Compiling_Lane.md` |
| D2.4 | Nullable §1–§10 | `Lambda_Design_Compiling_Nullable.md` |
| D2.5 | TE-15, TE-17, TE-18.1–TE-18.2; IEH I1–I4 | `Lambda_Design_Type_Enforcement.md`, `Lambda_Design_Type_Error_Handling.md` |
| D2.6 | SG1–SG8; SF15 | `Lambda_Design_Scalar_GC_Invariant.md`, `Lambda_Design_Stack_Frame.md` |
| D3.1 | OB1–OB23 | `Lambda_Type_Object.md` |
| D3.2 | Nullable §6; CW16; Int_Type §5.8 | `Lambda_Design_Compiling_Nullable.md`, `Lambda_Design_Runtime_COW.md` |
| D3.3 | LR09-R2/R3; Design_Syntax §7.27 | `Lambda_Issue_Ledger.md`, `Lambda_Design_Syntax.md` |
| D3.4 | Shape_Pool §1–§8; Shape_Transitions §1–§8; Transpiler DD1–DD4; NI10–NI18; TE §6 B7b; JOP1–JOP5; JSCU33 | `Lambda_Shape_Pool.md`, `Lambda_Design_Shape_Transitions.md`, `Lambda_Transpiler.md`, `Lambda_Design_Name_Identity.md`, `Lambda_Design_Structs_JS.md` |
| D3.5 | PDF_Output §5.4–§5.6 | `Lambda_Design_PDF_Output.md` |
| D4.1–D4.3 | C8.5-4, C9a; TE-1, TE-6, TE-10, TE-13, TE-19; DF12/DF13; B7, TI6; Lane §1 | `Lambda_Semantics_Formal2.md`, `Lambda_Design_Type_Enforcement.md`, `Lambda_Design_Type_Error_Handling.md`, `Lambda_Design_Type_Infer.md`, `Lambda_Design_Type_Generics.md`, `Lambda_Design_Compiling_Dual_Func.md` |
| D4.4 | Binary Schema §10 | `Lambda_Type_Binary_Schema.md` |
| D5.1–D5.2 | GC1 §2.10.4; CW8; SF16; CR8; Mem_Heap §1–§5, §9 (MP-12–MP-21); Memory_Context stages | `Lambda_Garbage_Collector.md`, `Lambda_Design_Mem_Heap.md`, `Memory_Context.md` |
| D5.3 | GC2 §4–§12 | `Lambda_Garbage_Collector2.md` |
| D5.4 | CW1–CW37 | `Lambda_Design_Runtime_COW.md` |
| D5.5 | Memory_Model §5–§7; Memory_Safety_Template §2.3; Reactive_UI §5.3, §10.1 | `Lambda_Design_Memory_Model.md`, `Memory_Safety_Template.md`, `Lambda_Design_Reactive_UI.md` |
| D5.6 | NI1–NI18, W1–W6 | `Lambda_Design_Name_Identity.md` |
| D5.7 | CP1–CP26 | `Lambda_Design_Const_Pool.md` |
| D6.1–D6.3 | SF1–SF20, OS1–OS11; Stack_API phases and invariants; CR1–CR8, RH1–RH8; RV1–RV16, RVO1–RVO11; RA1–RA15 | `Lambda_Design_Stack_Frame.md`, `Lambda_Design_Stack_API.md`, `Lambda_Design_Stack_Rooting.md`, `Lambda_Design_Compiling_Return_Value.md`, `Lambda_Design_Runtime_Async.md` |
| D6.4 | RG0–RG14, MT2 contract | `Lambda_Design_Runtime_Globals.md` |
| D7.1 | U14, U26; Features §3.6; NM §6.2; IEH §5.3; ES48; Design_Procedural §6 | `Lambda_Semantics_Features.md`, `Lambda_Design_Native_Module.md`, `Lambda_Design_DOM_Host_API.md`, `Lambda_Design_Procedural.md` |
| D7.2 | C8.7; Function_Arg; DF7/DF11; SF18; JC1–JC12; JSI5; Design_Procedural §5.3 | `Lambda_Design_Function_Arg.md`, `jube/JS_Runtime_Callable.md`, `Lambda_Design_JS_Interpreter.md`, `Lambda_Design_Procedural.md` |
| D7.3 | DF1–DF17, O1–O14 | `Lambda_Design_Compiling_Dual_Func.md` |
| D7.4 | LC1v2 and call-ABI notes; IR1–IR8; REH-D2–REH-D14 | `Lambda_Design_Compiling.md`, `Lambda_Design_JS_IC_Retire.md`, `Lambda_Design_Runtime_Error_Handling.md` |
| D7.5 | K11–K32 (runtime side); ER-D1/D11 | `Lambda_Design_Concurrency.md`, `Lambda_Design_Exec_Recovery.md` |
| D7.6 | Sys_Func §7–§8 | `Lambda_Design_Sys_Func.md` |
| D8.1 | CGP1–CGP21 | `Lambda_Design_Grammar_Parser.md`, `Lambda_Grammar_Parser.md` |
| D8.2 | U1–U36 | `Lambda_Design_Unified_AST.md`, `Lambda_Design_JS_Unified.md` |
| D8.3 | AI1–AI25, AIO1–AIO13; RI1–RI6 | `Lambda_Design_Ast_Interpreter.md`, `Lambda_Design_Repl_Interp.md` |
| D8.4 | JSI1–JSI36 | `Lambda_Design_JS_Interpreter.md`, `jube/JS_MVP_Lmd.md` |
| D8.5 | MC1–MC8; L3-1–L3-10 | `Lambda_Design_MIR_Cache.md`, `Lambda_Design_MIR_Cache_L3.md`, `Lambda_Design_Script_Cache.md` |
| D8.6 | MT1–MT8; U33–U36 | `Lambda_Design_MIR_Emission_Test.md`, `Lambda_Design_Unified_AST.md` |
| D9.1–D9.2 | SM1–SM17; Radiant_Split §2–§6 | `Lambda_Design_Static_Modules.md`, `Lambda_Design_Script_Cache.md`, `Lambda_Design_Radiant_Split.md` |
| D9.3 | RG14; DF15; ER-D2; MC1; Demo_Mail ML11 | `Lambda_Design_Runtime_Globals.md`, `Lambda_Design_Exec_Recovery.md`, `Lambda_Demo_Mail.md` |
| D9.4–D9.5 | JA1–JA16 (D9.4.6: JA5.1); Native_Module §6–§10; Lang_Hosting §5–§13; DOM4 D4j–D4l; Radiant_Split §4 | `Lambda_Design_Jube_Architecture.md`, `Lambda_Design_Native_Module.md`, `Lambda_Design_Jube_Lang_Hosting.md`, `Lambda_Design_Type_Virtual.md`, `Lambda_Jube_DOM4.md`, `Lambda_Design_Radiant_Split.md` |
| D10.1 | Mark file extensions; XHTML admission; SVG parsing admission and DOM export | `input/Input_System.md`, `input/Input_XML.md`, `radiant/Radiant_SVG2.md` |
| D10.2 | UA editing; TeX engine; Pkg_Math §8, §11 | `radiant/Radiant_Design_Editable.md` §20, `latex/Lambda_Design_TeX_Engine.md`, `Lambda_Pkg_Math.md` |
| D11.1 | JA10, JA16 | `Lambda_Design_Jube_Architecture.md` |
| D11.2 | App_Safety §4.1, §6.4, §7.1, §7.3, §8.3 | `Lambda_Design_App_Safety.md` |
| D11.3 | Speech §5; Scene3D §9.3 | `radiant/Radiant_Design_Speech.md`, `radiant/Radiant_Design_Scene3D.md` |

The decision records keep the full deliberations: every alternative that lost,
and the arguments that did not persuade. This specification is their
distillation. The records govern the history; this document governs the
design.

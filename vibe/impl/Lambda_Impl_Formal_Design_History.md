# Lambda Formal Design — Implementation History

**Date:** 2026-11-04
**Status:** informative. The formal spec
[`doc/Lambda_Formal_Design.md`](../../doc/Lambda_Formal_Design.md) wins on
every design point.
**Scope:** the implementation history of the design rulings, and the map from
the old ruling IDs to the IDs of spec 61.0.0.

Spec 61.0.0 (2026-11-04) restructured the Formal Design spec. It keeps only
principles and stable decisions, written in ASD-STE100 style. This document
keeps what the rewrite took out of the spec:

- §1 maps every old ruling ID to its new ID.
- §2 keeps the implementation footnotes of the old Appendix A, verbatim.
- §3 keeps the old Appendix B (open design issues) with its deliberation,
  verbatim, including the closed entries DO15, DO26 and DO30.
- §4 records how the rewrite was done and the conflicts that it found.

The old detailed design text of each ruling moved to the matching vibe design
record, under a section "Formal Design detail (relocated 2026-11-04)". The
complete text of spec 60.1.1 is in Git:
`git show 5faef303a:doc/Lambda_Formal_Design.md`.

In §2 and §3, the IDs use the new numbering, so they agree with the spec. The
text describes the state on its own dates.

## 1. Old-to-new ruling IDs

This is the authoritative map. `python3 utils/formal_index.py --old <ID>`
reads these tables, so keep their format. An old versioned ID maps by its
base ID, except where the Note column lists versions: those rulings were split
by content. Old IDs here are never migrated; elsewhere, write an old ID as
`ex-D8.1.1` so that it is not read as a new one.

| Old ID (60.1.1) | Old title | New ID | New title | Note |
|---|---|---|---|---|
| D1.1 | One host, many bundles | D1.1 | One host, many bundles |  |
| D1.2v2* | Explicit guest ABI boundary | D1.2 | `Item` is the only value currency at a host boundary |  |
| D1.3v3* | Shared host substrate, private guest cores | D1.3 | Guests share the host substrate |  |
| D1.4v4* | Language failures return through every frame | D1.4 | A language failure returns through every frame |  |
| D1.5v2* | Precise GC everywhere, forever | D1.5 | Precise GC everywhere, permanently |  |
| D1.6 | The legacy paths are frozen | D1.6 | MIR Direct is the only compiler back end |  |
| D1.7 | Source is the spec level | D1.7 | Source is the distribution format |  |
| D1.8 | Decisions are carried, never recovered | D1.8 | Carry decisions forward; never recover them |  |
| D1.9 | Fail closed | D1.9 | Fail closed |  |
| D1.10 | Enforcement is part of the design | D1.10 | Enforcement is part of the design |  |
| D2.1.1 | An `Item` is one 64-bit word, passed by value, in exactly three storage classes: **inline tagged immediate** … | D2.1.1 | An Item is one word with three storage classes | merged with D2.1.3 |
| D2.1.2 | `get_type_id(Item)` is the **only** sanctioned semantic-type interface (tag ≠ 0 → tag; | D2.1.2 | One interface gives the semantic type |  |
| D2.1.3 | Measured ruling: String/Symbol/Binary stay **tagged leaf pointers** | D2.1.1 | An Item is one word with three storage classes | merged with D2.1.1 |
| D2.1.4 | Container extensibility is **layered, not tag-based**: storage class → `Container.type_id` family → … | D2.1.3 | Extend containers by layers, not by tags |  |
| D2.1.5 | Canonical construction: one encoding per value per build; | D2.1.4 | Construction is canonical |  |
| D2.1.6* | *A storage class is not designed until its lifecycle is* | D2.1.5 | Design the lifecycle with the storage class |  |
| D2.1.7 | One-way doors, deliberately accepted and to be re-acknowledged before GC/sandboxing/port work: non-moving … | D2.1.6 | Some decisions are one-way doors |  |
| D2.2.1 | Common doubles are **self-tagged inline** (the double's own bit pattern is the Item; | D2.2.1 | Common doubles are inline |  |
| D2.2.2 | The v5 `int` carrier: finite values pack into the 56-bit payload (unbox = shift-left-8, … | D2.2.2 | The int carrier has a private native lane |  |
| D2.2.3 | Wide scalars (`i64`, `u64`, cold doubles) have **no inline form at any magnitude** (no value-dependent … | D2.2.3 | Wide scalars have no inline form |  |
| D2.2.4 | `LMD_TYPE_DECIMAL` carries a `Decimal` wrapper with an explicit `DecimalKind storage_kind` (`FIXED`, … | D2.2.4 | Decimal tiers are invisible |  |
| D2.2.5 | MVP-Lmd admits Lambda `int` as an internal runtime subtype of JS Number | D2.2.5 | In MVP-Lmd, Lambda `int` is a private subtype of JS Number |  |
| D2.3.1 | A typed function compiles to a **native entry plus a boxed entry** per the dual-func plan (§D8.3); | D2.3.2 | A typed function has a native entry and a boxed entry |  |
| D2.3.2* | Blind `(void*)` casts of Items to container pointers are a defect | D2.3.3 | Never cast an Item to a container pointer blindly |  |
| D2.3.3 | Boxing is representation only (semantics S1.6); | D2.3.1 | Boxing is representation only |  |
| D2.4.1* | Four facts, four authorities: semantic contract → AST `Type*`; | D2.3.4 | Four facts have four owners | merged with D2.4.2 |
| D2.4.2* | `transpile_expr()` returns a `MirValue` carrying the full `Type*` contract; | D2.3.4 | Four facts have four owners | merged with D2.4.1 |
| D2.4.3* | Representation conversion ≠ semantic coercion | D2.3.5 | A carrier conversion is not a semantic coercion |  |
| D2.5.1* | `N(T?) = N(T) \| NULL_LANE(T)`: optionals keep a native carrier everywhere a representation is chosen | D2.4.1 | An optional keeps a native carrier |  |
| D2.5.2v3 | Sentinels: `INT_LANE_NULL` (numerically the `ItemNull` word | D2.4.2 | Each lane has one null code |  |
| D2.5.3 | `a[i]` with an unproven index infers `T?` | D2.4.3 | Representation follows the full inferred contract | merged with D3.3.4 |
| D2.6.1v3 | Three physical array forms: boxed Array (Items), native Array (uniform descriptor-selected native slot per … | D3.2.1 | There are three physical array forms | merged with D2.6.3 |
| D2.6.2 | ArrayNum is strictly non-null | D3.2.2 | ArrayNum is non-null and invisible |  |
| D2.6.3 | `ELEM_INT` element storage is the i64 lane (finite values or poison sentinels), mapped to IEEE at print/box … | D3.2.1 | There are three physical array forms | merged with D2.6.1 |
| D2.6.4v3 | A container that retains a raw wide-scalar Item points **only into that container's own buffer** (tail … | D2.6.2 | A container owns its wide scalars |  |
| D2.6.5v4 | The append discipline follows the destination: content, sequence, or slot | D3.3.1 | The destination selects the append |  |
| D2.6.6v3* | One container hierarchy — map → array → element — and nominal is a descriptor property, not a kind | D3.1.1 | One container hierarchy: map, array, element | merged with D2.6.11 |
| D2.6.7* | A bound method is an ordinary D6.2.1 function value whose closure environment is the receiver, captured by … | D3.1.4 | A bound method is a closure over its receiver |  |
| D2.6.8* | Node identity is data the container carries, never its address | D3.1.3 | Node identity is data, never an address |  |
| D2.6.9v3* | A JavaScript object is a nominal Lambda `Map` — and therefore a Lambda object | D3.1.5 | A JavaScript object is a nominal Lambda map |  |
| D2.6.10* | Sealing at the representation level | D3.1.2 | Sealed types permit cached layout |  |
| D2.6.11* | The `is_nominal` bit packs into the existing header | D3.1.1 | One container hierarchy: map, array, element | merged with D2.6.6 |
| D2.6.12v3* | Virtual output lists carry their builder | D3.5.1 | A virtual output list carries its builder |  |
| D2.7.1 | No standalone scalar cell in the GC heap | D2.6.1 | No standalone scalar cell in the GC heap |  |
| D2.7.2v2* | Public entry wrappers speak the **companion-lane pair** (D5.2.1v5); | D6.2.1 | Wide returns use a companion lane | merged with D5.2.1 |
| D2.8.1 | No native lane slot ever holds an error | D2.5.1 | No native lane holds an error |  |
| D2.8.2* | No unboxed operator ever receives an error operand | D2.5.2 | The emitter keeps errors out of unboxed operators |  |
| D2.8.3v3* | Lane entry is gated on static proof, never on runtime rescue | D2.5.3 | Lane entry needs a static proof |  |
| D2.9.1 | `.mark` is the sole canonical file extension for Mark Notation | D10.1.1 | `.mark` is the one Mark file extension |  |
| D2.9.2 | An explicit `mark` format overrides the filename | D10.1.2 | An explicit format overrides the filename |  |
| D2.9.3 | A domain document written in Mark takes its own format name and extension; it is not a Mark alias | D10.1.3 | A domain document in Mark syntax has its own format |  |
| D2.10.1v2 | `xhtml` uses XML syntax and emits the HTML element tree | D10.1.4 | XHTML uses XML syntax and gives the HTML element tree |  |
| D2.11.1 | SVG embedded in `text/html` follows browser-standard HTML parsing | D10.1.5 | SVG always uses HTML5 SVG parsing | merged with D2.11.2 |
| D2.11.2 | External SVG reuses that same HTML5 SVG parsing path and result model | D10.1.5 | SVG always uses HTML5 SVG parsing | merged with D2.11.1 |
| D2.11.3 | SVG source export serializes the current SVG DOM, including edits | D10.1.6 | SVG export serializes the current DOM |  |
| D2.11.4* | Animation snapshots use the frame selected by the virtual timer | D10.1.7 | An animation snapshot uses the virtual-timer frame |  |
| D3.1.1v5* | A type value is a `Type*` graph node under one compact TypeId (`LMD_TYPE_TYPE`), discriminated by `Type.kind` … | D4.1.1 | A type value is a `Type*` graph |  |
| D3.1.2 | Types compose as values (`\|` union, `?`, `[]`, constraints) and compare **representationally** | D4.1.2 | Types compose as values and compare by representation |  |
| D3.1.3 | Reflection is by operators and functions, not properties: `type(x)` yields the type value, `name(T)` its name; | D4.1.3 | Reflection uses operators and functions |  |
| D3.1.4v2 | A type-binder site stores its written bound, name, and stable signature-local slot; | D4.1.4 | A type binder has a stable slot |  |
| D3.2.1 | One subtype model, three distinct operations | D4.2.1 | One subtype model, three operations |  |
| D3.2.2v3* | The **validator is the runtime enforcer** for user-defined types: deep, on first crossing, producing a rich … | D4.2.3 | The validator enforces user-defined types at run time |  |
| D3.2.3v3* | Declared and effective types are **separate recorded facts** (declaration on the binding node, inference … | D4.2.4 | Declared and effective types are separate facts |  |
| D3.2.4v5* | A crossing into a named map contract is a reification, not merely a check | D4.2.5 | A crossing into a named map contract reifies its layout |  |
| D3.2.5v2 | The surface spelling for the shared subtype relation is the binary `<:` operator | D4.2.2 | `<:` spells the subtype relation |  |
| D3.2.6* | Verified once, valid while unchanged; a required field is a required field | D4.2.6 | Verified once, valid while unchanged |  |
| D3.3.1v2 | A type-error-free script's evaluation result is never affected by inference | D4.3.1 | Inference never changes the result of a correct program |  |
| D3.3.2v2 | Entry-shape inference and body/result inference are **separate products**: float arithmetic on a parameter … | D4.3.2 | Inference selects an implementation, never a contract |  |
| D3.3.3v3 | Inferred container element-type **narrowing dies with its binding**: it is a property of one binding's scope, … | D4.3.3 | Inferred narrowing dies with its binding |  |
| D3.3.4 | Representation always follows the **full inferred contract** (D2.4, D2.5): an unproven indexed read infers … | D2.4.3 | Representation follows the full inferred contract | merged with D2.5.3 |
| D3.3.5 | System-function result relations are declarative registry metadata | D4.3.4 | System-function result relations are registry metadata |  |
| D3.4.1 | A map/element shape is a `ShapeEntry` chain on `TypeMap`/`TypeElmt`; | D3.4.1 | Shape layout is ABI |  |
| D3.4.2v2 | Shape identity is **structural, not nominal**: the ordered (field-name, TypeId) sequence, which is the path a … | D3.4.2 | Shape identity is structural |  |
| D3.4.3v5 | Maps and elements share types through a per-`Input` transition tree, grown one field at a time | D3.4.3 | Shapes share through a transition tree |  |
| D3.4.4v4* | ShapeEntry name identity follows S8.2.2v4: a name is identified by its resolved namespace and its normalized … | D3.4.4 | Shape names follow the name identity rule |  |
| D3.4.5 | Shape transitions obey the **map-layout invariant**: the exact runtime shape always describes the stored bytes | D3.4.5 | The exact shape always describes the stored bytes |  |
| D3.4.6 | Shapes carry the immutable `LaneStorageDesc` derived from the full `Type*` via the one shared descriptor … | D3.4.6 | Shapes carry the lane descriptor |  |
| D3.4.7 | A runtime JavaScript `TypeMap` may carry one immutable `const JsClassMeta*` refinement selected before … | D3.4.7 | JavaScript class metadata refines runtime shapes | merged with D3.4.8 |
| D3.4.8 | A JavaScript accessor descriptor is a virtual, object-local `ShapeEntry`: it owns a `JsAccessorCell*`, has … | D3.4.7 | JavaScript class metadata refines runtime shapes | merged with D3.4.7 |
| D3.5.1v2* | Binary schemas compile to immutable, module-owned field descriptors; | D4.4.1 | Binary schemas compile to immutable descriptors |  |
| D3.5.2v2* | Native schema views retain an invalidatable lifetime control; | D4.4.2 | A native schema view can be made invalid |  |
| D4.1.1v2 | Four **content tiers**: **GC heap** (runtime values), **Input arena** (parsed documents), **AST/const pool**, … | D5.1.1 | There are four content tiers |  |
| D4.1.2 | Static Mark data is **born shared and immortal**: `MarkBuilder` sets the shared COW state at construction; | D5.1.2 | Static Mark data is born shared and immortal |  |
| D4.1.3 | Input/format allocations are pool/arena-owned and **outside GC rooting entirely**; | D5.1.3 | Input data is outside GC rooting |  |
| D4.1.4v5* | Heap bytes are kept by exactly **four mechanisms** | D5.1.4 | There are four allocation mechanisms |  |
| D4.1.5* | Two mechanism-selection corollaries of D4.1.4 | D5.1.5 | Select the mechanism by lifetime |  |
| D4.2.1v3 | `MemContext` is a **factory that owns every allocator** | D5.2.1 | `MemContext` owns every allocator | merged with D4.2.3 |
| D4.2.2v2 | Per-document sub-contexts key allocators to a document URL, with the **attribution/reclamation split**: … | D5.2.2 | Documents get attribution; some also get reclamation |  |
| D4.2.3* | Every arena and pool is **bound at creation to a context** | D5.2.1 | `MemContext` owns every allocator | merged with D4.2.1 |
| D4.2.4* | A shared arena/pool's lifetime is governed by an **allocator-level atomic `ref_count`**: created at 1 held by … | D5.2.3 | A shared allocator has a reference count |  |
| D4.2.5v3* | Allocation tracking is a **diagnostic, never the mechanism**: release builds default `memtrack` OFF; | D5.2.4 | Allocation tracking is a diagnostic |  |
| D4.2.6 | A pool may **own a resource that is not one of its blocks** through a **cleanup**: `pool_add_cleanup` … | D5.2.5 | A pool can own an external resource through a cleanup |  |
| D4.3.1 | Non-moving mark-and-sweep, dual-zone | D5.3.1 | The collector is a non-moving, two-zone mark-and-sweep | merged with D4.3.2 |
| D4.3.2v2 | Object zone = size-class segregated free lists backed by VM-owned extents; | D5.3.1 | The collector is a non-moving, two-zone mark-and-sweep | merged with D4.3.1 |
| D4.3.3 | Precise closure-env tracing via `closure_field_count`; | D5.3.2 | Closures and virtual maps are traced precisely |  |
| D4.3.4 | A shaped field whose declared lane is `null` is traced **conservatively**: the collector marks the eight-byte … | D5.3.3 | A null-typed shape lane is traced as a possible pointer |  |
| D4.4.1 | v1 ships the **1-bit shared flag** (`cow_state`, monotonic, idempotent; | D5.4.1 | COW uses one shared bit |  |
| D4.4.2 | A COW copy is exactly **one level** | D5.4.2 | A COW copy is one level |  |
| D4.4.3 | All copy paths precisely root source/replacement/owner chain and reload after possible GC; | D5.4.3 | Copy paths stay rooted |  |
| D4.4.4v4* | A place handle | D5.4.4 | A place handle can borrow its place |  |
| D4.4.5 | A **move-out** place copy | D5.4.5 | A place copy is share-marked if its place can change | merged with D4.4.6 |
| D4.4.6 | A **place copy** (`let`/`var h = root.path`, root mutable) is share-marked at its bind iff **the place may be … | D5.4.5 | A place copy is share-marked if its place can change | merged with D4.4.5 |
| D4.4.7 | A place passed to a `var` parameter uses a caller-owned, precisely rooted Item home and evaluated path keys | D5.4.6 | A `var` argument uses a caller-owned home |  |
| D4.5.1v4 | One system-allocation substrate (`memtrack` + VM regions, owned through `MemContext`), **two policies**: … | D5.5.1 | One substrate, two policies |  |
| D4.5.2* | Radiant never retains a GC pointer | D5.5.2 | Radiant never keeps a GC pointer |  |
| D4.5.3v2 | UI/view-mode DOM construction deep-copies ordinary Elements and their content through MarkBuilder | D5.5.3 | DOM construction copies; presentation retains |  |
| D4.6.1v4 | One semantic **property** identity is a `NameId`, never a `String*` address | D5.6.1 | A `NameId` is property identity |  |
| D4.6.2v2 | Evolve NamePool, don't replace it (first definer wins and parent-first lookup) | D5.6.2 | Evolve the NamePool; do not replace it |  |
| D4.7.1* | One binary encoding for Lambda data, four consumers | D5.7.1 | One binary encoding has four users |  |
| D4.7.2* | Two stream variants over one encoding (bare stream; | D5.7.2 | Sealing appends; stream bytes never change |  |
| D5.1.1v3 | Three stack-like mechanisms with distinct owners: the native C stack (invisible to GC); | D6.1.1 | Three stack mechanisms have different owners |  |
| D5.1.2 | Static per-function frame sizing, no `MIR_ALLOCA`, two saved watermarks per frame, virtual reservation + … | D6.1.2 | Frames are static |  |
| D5.1.3 | Downward flows never re-home; only upward flows and outliving stores do. Suspension is a re-homing barrier | D6.1.3 | Only upward flows and outliving stores re-home a value |  |
| D5.1.4 | Cleanup order is load-bearing: all `MAY_GC` cleanup runs **before** root/number watermark restoration; | D6.1.4 | Cleanup runs before the watermarks are restored |  |
| D5.2.1v5* | Function wide-scalar returns use the **companion-lane convention**: a may-be-wide boxed return is the pair … | D6.2.1 | Wide returns use a companion lane | merged with D2.7.2 |
| D5.2.2v3 | Scalar homes are raw payload words, never GC roots, colored in a separate slot space from root slots; | D6.2.2 | A scalar home is a payload word owned by a destination |  |
| D5.2.3* | Watermark ownership selects the wide-return transport | D6.2.3 | Watermark ownership selects the wide-return path |  |
| D5.3.1 | Generated code uses **safepoint-current canonical slots**: a stable home per rootable binding plus colored … | D6.3.1 | Generated code uses safepoint-current canonical slots |  |
| D5.3.2 | GC begins only inside a `MAY_GC` call | D6.3.2 | A collection starts only in a `MAY_GC` call |  |
| D5.3.3 | C/C++ helpers use slot-backed `RootFrame` / `Rooted<T>` / handles / `PersistentRooted` on the same side-root … | D6.3.3 | Native helpers root through slot-backed frames |  |
| D5.3.4 | Rootability is driven by the four representation classes (`BOXED_ITEM`, `RAW_GC_POINTER`, `NON_GC_SCALAR`, … | D6.3.4 | The representation class decides rootability |  |
| D5.3.5 | Argument passing reserves each function's **maximum lexically-overlapping argument arity as a fixed suffix of … | D6.3.5 | Arguments use a fixed frame suffix |  |
| D5.3.6v2 | Recovery frames form **one LIFO chain per activation** (the thread's base stack is one), a frame per boundary … | D6.3.6 | Recovery frames form one chain per activation |  |
| D5.4.1 | One canonical long-lived `EvalContext` per isolate; exactly one sanctioned TLS root | D6.4.1 | One `EvalContext` per isolate and one TLS root |  |
| D5.4.2 | Context state lives in lazily-allocated opaque capsules; | D6.4.2 | Context state lives in opaque capsules |  |
| D5.4.3 | No context-dependent value at a code-baked address | D6.4.3 | No context value is at a fixed address in code |  |
| D5.4.4 | Frozen registries stay global (immutable after publication); | D6.4.4 | Registries are frozen; hot paths stay lock-free |  |
| D6.1.1v2 | `fn`/`pn` is a one-bit, declared, compiler-checked effect system (semantics §S12.1), realized as … | D7.1.1 | `fn`/`pn` is infrastructure | merged with D7.4.6 |
| D6.1.2v2* | The bit is load-bearing across the runtime: it is the const-folder's soundness gate, the stream-fusion and … | D7.1.2 | The bit is load-bearing |  |
| D6.1.3v2* | Effect analyses beyond the bit are separate compiler facts: `may_await` (the call may park, so it may collect … | D7.1.3 | Other effects are separate facts that fail closed |  |
| D6.2.1 | A function value is a GC object carrying its entry pointers, its attached type/signature, and its closure … | D7.2.1 | A function value has a definition-site identity |  |
| D6.2.2v2 | Dynamic calls dispatch through per-callee executable entries | D7.2.2 | Dynamic calls use per-callee entries |  |
| D6.2.3v3* | Closure representation respects capture permissions | D7.2.3 | Closures respect capture permissions |  |
| D6.2.4 | The closure env is precisely traced (`closure_field_count`); | D7.2.4 | A closure environment is traced precisely and stays boxed |  |
| D6.3.1 | Tasks share the context heap under **one scheduler per context**; | D7.5.1 | One scheduler per context |  |
| D6.3.2* | Workers are **share-nothing isolates** (thread default, process optional); | D7.5.2 | Workers are share-nothing isolates |  |
| D6.3.3v2 | Faults are scoped to one thread and one stack | D7.5.3 | A fault stays in one thread and one stack |  |
| D6.4.1 | Public sys funcs return **boxed `Item`** at the runtime/JIT boundary; | D7.6.1 | Public sys funcs return boxed Items |  |
| D6.4.2 | Raw `-1` helpers are **adapter-only**; | D7.6.2 | Raw helpers are only for adapters |  |
| D7.1.1 | Build packaging is **static libraries** (dynamic is reserved for Jube modules): four archives + exe | D9.1.1 | Static libraries in a strict layer order |  |
| D7.1.2v2* | All resource IO lives in `lambda-io` | D9.1.2 | All resource IO lives in `lambda-io` |  |
| D7.1.3 | Provider ownership beats include ownership | D9.1.3 | Provider ownership beats include ownership |  |
| D7.1.4v2 | Headless is **three profiles**: A (`lambda-cli`, D7.1.6) is a link-time omission of `radiant.a`; | D9.2.1 | Headless has three profiles |  |
| D7.1.5 | Mark API ownership: `MarkReader` is core; | D9.1.4 | Mark API ownership resolves the layering |  |
| D7.1.6 | `lambda-cli` is the runtime-only host | D9.2.2 | `lambda-cli` is the runtime-only host |  |
| D7.1.7v6 | `lambda-wasm` is the browser evaluation profile | D9.2.3 | `lambda-wasm` is the browser evaluation profile |  |
| D7.2.1 | A Lambda script module (package) is the compilation unit: its language profile is a unit property (D8.2.1), … | D9.3.1 | A package is the compilation unit |  |
| D7.2.2 | Imports resolve to the **boxed `_b` symbol** of exported functions (export ⇒ boxed entry mandatory, D8.3.4v4); | D9.3.2 | Imports bind boxed entries; initialization is a transaction |  |
| D7.2.3 | Imported packages are cached in-process (L1, D8.5.1) and distribute as **source** (D1.7); | D9.3.3 | Packages are cached in-process and distributed as source |  |
| D7.2.4v4 | `lambda.*` is the one root for everything Lambda ships | D9.3.4 | `lambda.*` is the one root for shipped code |  |
| D7.2.5* | Full user-agent editing policy belongs to the shipped Lambda DOM behavior package, never to Radiant's native … | D10.2.1 | The DOM behavior package owns the editing policy |  |
| D7.2.6 | Import resolution follows the S16.9.8 roots, one resolver for every caller | D9.3.5 | One import resolver serves every caller |  |
| D7.2.7v2* | LaTeX is read by TeX expansion, never by TeX typesetting | D10.2.2 | LaTeX is read by TeX expansion, never by TeX typesetting |  |
| D7.2.8* | Document math preserves its source notation's layout policy | D10.2.3 | Document math keeps the layout policy of its notation |  |
| D7.2.9 | Bundled document fonts are shared file resources | D10.2.4 | Bundled fonts are shared resources |  |
| D7.3.1 | Core = ECMA-262 + engine substrate + `./lib` + a **closed globals allowlist** (console, timers, … | D9.4.1 | The core is closed; everything else is a module |  |
| D7.3.2 | Modules are DSOs + `module.json`: hash-verified, ABI and hosted-API negotiated before any module code runs, … | D9.4.2 | Modules are verified, negotiated and transactional |  |
| D7.3.3 | One strict host-API tier for everyone | D9.4.3 | There is one strict host-API tier |  |
| D7.3.4 | Core never links modules; | D9.4.4 | No module links another |  |
| D7.3.5 | Every module kind names its conformance gate, wired into CI | D9.4.5 | Every module kind names its conformance gate |  |
| D7.3.6* | The host exports an allowlist, nothing more | D9.4.6 | The host exports only an allowlist |  |
| D7.4.1v2 | `Item` is the only value currency; | D9.5.1 | Items cross; host structs use virtual carriers |  |
| D7.4.2v2 | Modules are **fully shielded from the async substrate** | D9.5.2 | Modules are shielded from the async substrate |  |
| D7.4.3 | Guest hosting crosses **no core types**: no `Runtime*`, `EvalContext*`, `Input*`, `MIR_context_t`, or … | D9.5.3 | Guest hosting crosses no core types |  |
| D7.4.4 | Declared interfaces + record-owned hooks are the ONLY host-object protocol — one way to be a host object, no … | D9.5.4 | Declared interfaces are the only host-object protocol |  |
| D7.4.5v2* | Three virtual carriers mirror the container taxonomy and occupy their structural TypeId bands: `vmap`, … | D9.5.5 | Three virtual carriers mirror the container taxonomy |  |
| D7.4.6 | A host module states each function's colour in its signature | D7.1.1 | `fn`/`pn` is infrastructure | merged with D6.1.1 |
| D7.5.1 | Three trust tiers: **T1 system** (in-process DSO | D11.1.1 | There are three trust tiers |  |
| D7.5.2* | One central IO API is the sole IO door | D11.1.2 | One central IO API is the only IO door |  |
| D7.5.3v2 | Lambda↔Radiant is two explicit contracts: Radiant embeds Lambda through a versioned, Jube-aligned embed … | D9.5.6 | Lambda and Radiant meet through two explicit contracts |  |
| D7.5.4* | Audible output is a capability gated by document origin | D11.3.1 | Audible output depends on the document origin |  |
| D7.5.5* | Apps and ad-hoc scripts execute within an admitted source/resource root | D11.2.1 | Apps and scripts run inside an admitted root |  |
| D7.5.6* | New files under the app/project root are allowed by default; overwriting existing files requires user approval | D11.2.2 | Create freely; overwrite only with approval |  |
| D7.5.7* | Per-app and per-ad-hoc-script grants live under `~/.lambda/setting`, controlled by the trusted host | D11.2.3 | The trusted host owns the grant settings |  |
| D7.5.8* | App/script network access is denied by default; package auto-update is a separate host-owned pull service | D11.2.4 | Network access is denied by default; updates are a host service |  |
| D7.5.9* | Uninstall retains private app data by default | D11.2.5 | Uninstall keeps private app data |  |
| D7.5.10* | Microphone input requires explicit acquisition and separate grants | D11.3.2 | The microphone needs explicit acquisition and separate grants |  |
| D8.1.1v18* | First-party Lambda C lexer + hybrid recursive-descent/Pratt parser → the shared typed AST → **tiered … | D8.3.1 | The AST interpreter is the baseline tier | v5, v14, v16, v18 → D8.3.2; v6, v7, v8, v9, v10 → D8.3.3; v12 → D8.3.4; v11, v13 → D8.3.5; v17 → D8.3.6 |
| D8.1.2v3 | The shipped Lambda executable uses the first-party C parser as the final production implementation for normal … | D8.1.1 | First-party C parsers are the production front ends |  |
| D8.1.3v43* | LambdaJS uses its first-party C lexer and hybrid recursive-descent/Pratt parser for normal JavaScript and … | D8.4.1 | One runtime, separate semantic walkers | v11, v12, v19, v25, v27, v28, v29 → D8.4.2; v20, v21, v26 → D8.4.3; v43 → D8.4.4; v13, v14, v15, v16, v17, v18, v22, v23, v24, v30, v31, v32, v33, v34, v35, v36, v37, v38, v39, v40, v41, v42 → D8.4.5 |
| D8.2.1* | One leveled core-node catalog serves host and guests; | D8.2.1 | Language is a property of the compilation unit |  |
| D8.2.2* | Variance tiers in strict preference order: fields/flags → clause chains → language-range node kinds; | D8.2.2 | Keep syntax variance as fields; never rewrite the tree |  |
| D8.2.3v2* | Migration is extract-after-convergence: the common core is extracted only after two working clients exist; | D8.2.3 | Extract the common core after convergence |  |
| D8.2.4v2* | The unified compiler has one **indexed compilation unit**: dense stable IDs name … | D8.2.4 | One indexed compilation unit |  |
| D8.2.5v3* | One typed pass manager owns build → bind → validate → index → collect → capture/effect → environment layout → … | D8.2.5 | One typed pass manager; facts live on the AST |  |
| D8.2.6v2* | Core expression lowering is demand-driven and returns the full `MirValue` of D2.4.2 | D8.2.6 | Lowering is driven by demand |  |
| D8.3.1v2* | Plan-dependent entries comprise the boxed `<name>_b` boundary and a **bounded set of immutable raw variants** | D7.3.1 | A boxed boundary plus a bounded set of raw variants |  |
| D8.3.2 | The check lives in the callee | D7.3.2 | The check lives in the callee |  |
| D8.3.3 | Source-relative correctness is the invariant | D7.3.3 | Correctness is relative to the source |  |
| D8.3.4v4 | Visibility decides which versions exist | D7.3.4 | Visibility decides which entries exist |  |
| D8.3.5 | Numeric admission at implicit boundaries is one rule (static whole-domain embedding admits + normalizes; | D7.3.5 | One numeric admission rule |  |
| D8.4.1v2 | No inline caches anywhere in the Lambda lane or LambdaJS | D7.4.1 | No inline caches |  |
| D8.4.2v2* | Core direct calls pass individual ABI operands (`Context*`, args); | D7.4.2 | Direct calls pass ABI operands |  |
| D8.4.3v2* | Every hosted-language helper uses an explicit completion ABI | D7.4.3 | Every guest helper uses an explicit completion ABI |  |
| D8.5.1v7* | L1 | D8.5.1 | The L1 script cache is in-process | v6 → D8.3.4; v5, v7 → D8.3.5 |
| D8.5.2* | The disk-cache direction is **code image + relocation journal** (Route B); | D8.5.2 | A disk cache stores a code image with a relocation journal |  |
| D8.5.3* | The **differential write verifier is mandatory and fail-closed**: compile twice at different addresses, diff; | D8.5.3 | Every cache entry is born verified |  |
| D8.6.1 | The MT7 ratchet: 0% slack | D8.6.1 | The emission ratchet has 0% slack |  |
| D8.6.2 | One artifact contract: the MIR dump path honored in release; | D8.6.2 | One artifact contract |  |
| D8.6.3 | Liveness is verified by **dynamic oracles** | D8.6.3 | Dynamic oracles verify liveness |  |
| D8.6.4v2* | Unified-AST consolidation has three **hard, fail-closed exit ratchets**: at least 2,000 net physical C/C++ … | D8.6.4 | The unified-AST consolidation has hard exit ratchets |  |

| Old section | New section |
|---|---|
| §D1 | §D1 |
| §D2 | §D2 |
| §D3 | §D4 |
| §D4 | §D5 |
| §D5 | §D6 |
| §D6 | §D7 |
| §D7 | §D9 |
| §D8 | §D8 |
| §D2.1 | §D2.1 |
| §D2.2 | §D2.2 |
| §D2.3 | §D2.3 |
| §D2.4 | §D2.3 |
| §D2.5 | §D2.4 |
| §D2.6 | §D3 |
| §D2.7 | §D2.6 |
| §D2.8 | §D2.5 |
| §D2.9 | §D10.1 |
| §D2.10 | §D10.1 |
| §D2.11 | §D10.1 |
| §D3.1 | §D4.1 |
| §D3.2 | §D4.2 |
| §D3.3 | §D4.3 |
| §D3.4 | §D3.4 |
| §D3.5 | §D4.4 |
| §D4.1 | §D5.1 |
| §D4.2 | §D5.2 |
| §D4.3 | §D5.3 |
| §D4.4 | §D5.4 |
| §D4.5 | §D5.5 |
| §D4.6 | §D5.6 |
| §D4.7 | §D5.7 |
| §D5.1 | §D6.1 |
| §D5.2 | §D6.2 |
| §D5.3 | §D6.3 |
| §D5.4 | §D6.4 |
| §D6.1 | §D7.1 |
| §D6.2 | §D7.2 |
| §D6.3 | §D7.5 |
| §D6.4 | §D7.6 |
| §D7.1 | §D9.1 |
| §D7.2 | §D9.3 |
| §D7.3 | §D9.4 |
| §D7.4 | §D9.5 |
| §D7.5 | §D11 |
| §D8.1 | §D8.3 |
| §D8.2 | §D8.2 |
| §D8.3 | §D7.3 |
| §D8.4 | §D7.4 |
| §D8.5 | §D8.5 |
| §D8.6 | §D8.6 |

Invariants keep their numbers. DI15v2 became DI15. Open issues keep their
numbers. The second DO25 (interpreter tier breadth) became DO33. DO15, DO26 and
DO30 are closed and appear only in §3.

## 2. Implementation footnotes

Status of `*`-marked rulings as of 2026-10-07.

**D1.2, D1.3, D1.4, and D1.5** remain partially implemented. The
independent JS MVP profile that exercised D1.2/D1.3's private guest ABI and
D1.5's private-root contract was removed from the active tree on 2026-10-07.
Its source and release measurements are historical; see
`vibe/jube/JS_MVP_Runtime.md`. The production LambdaJS path continues to use
the shared `Item` and precise-GC substrate. Any future private guest runtime
must still satisfy the explicit host-boundary, completion, and rooting
contracts in these rulings.

The D8.2.4–D8.2.6 implementation record now includes P3j, P4l, and the
post-P6 binding/identity schedule work (2026-08-31):
`CompilerPassSpec` carries an optional pass-owned context, and the shared
`AstIndexPassContext`/`ast_index_compiler_pass` callback is used by Lambda and
LambdaJS. The JS driver has discrete manager-owned `analyze-plan`,
`mir-lower`, `mir-finalize-load`, and static `prelink` operations; Lambda MIR Direct has matching
plan, lower, and finalization/load operations before its established entry
link. Builder-published `NameEntry` links and FunctionId-keyed
`func_entries_by_id` replace lowering-time name and callable recovery;
direct class-entry consumers map the common nearest `AstClassId` to their
index-ordered backend entries, while nested/private class resolution, subtree
membership, descendant capture writes, and subtree visitors use indexed
structural ancestry/adjacency rather than source ranges or backend order recovery; module declaration caching consumes the same
binding-definition edge. Function backend relationships now carry their sealed
`AstFunctionId`: parent lookup follows that ID through the published
`func_entries_by_id` table, and the active lowering function is its
published backend record rather than a post-order table position. JS source, eval, module, and cached-AST execution use one
recovery-aware entry helper. The Lambda driver conditionally schedules the
indexed prerequisite when a retained AST has not already published it, so
build-to-entry is one authoritative schedule. Indexed
suspension/assignment/return/local/constructor facts and sparse
scope-environment writeback now use the AST-owned function identity; the
recursive mirrors are retired. The follow-on P4l slice retires duplicate
lexical-key, module-block, and `for`-initializer walks in favor of the indexed
scope chain and shared child contract, and reuses `em_frame_dispose()` for frame
teardown. Post-link BSS/template publication and final owner transfer remain
explicit runtime handoff work under **D8.2.5**.

The post-P6 FunctionId fact-table follow-up (2026-08-31) puts JavaScript
strictness, IIFE promotion, class/constructor/field-initializer status, and
TCO eligibility on the source function's `FnAnalysis`; collection publishes
them after its backend link resets the analysis record, and lowering consumes
those facts without re-reading directive syntax or using a post-order entry
flag. The shared IIFE-body predicate retires repeated current-function tests.
The next follow-up also stores lexical class ownership as `AstClassId` on that
analysis record and resolves its MIR class entry only at a backend-use boundary;
`NATIVE_RETURN_NONE` is the sole no-native plan state, replacing the duplicate
native-version flag. Copied-closure parent-link, reusable-parent environment,
direct-parent-link, parent-link-direction, and native-return classification
plan/slot facts are likewise source function facts on `FnAnalysis`; the
synthetic module carrier and MIR entry handles remain backend artifacts because
they have no source `FunctionId`. This is an implementation-only **D8.2.4**
status update; no formal ruling or document semver changes.

The JavaScript C front end now has a manager-owned `parse-build` → `bind` →
`validate` → `index` schedule under **D8.2.5**. Parsing and binding failures
leave no published AST; an early-error validation failure retains its AST and
existing diagnostic state but cannot publish `INDEXED`. Automatic JavaScript /
TypeScript and module selection enter the same C-parser boundary. That manager
is retained by `JsTranspiler` and resumes at appended `analyze-plan` →
`mir-lower` → `mir-finalize-load` → `prelink` passes, rather than re-seeding
front-end facts in a second manager. The AST owner is also the MIR pass root.
Static property-key prelink is manager-owned; runtime module-state linking
is a manager-owned execution-boundary `runtime-link` pass appended after
module-state activation. Lambda's active `Transpiler` now starts that same
compiler unit with `parse` → `build` → `bind` → `validate` → `index`, then
carries it through const-fold, planning, MIR lowering, finalization/load, and
entry link; retained-AST fallback starts a fresh manager from its already
indexed unit. The `parse` pass drives a syntax sink that allocates each
retained AST node as its production completes and records only source facts:
names are interned but unbound and read as `any`, and no scope, constant,
type or diagnostic is published. A syntax error leaves the nodes built so far
in the unit's pool, unpublished. `build` is the resolve pass: it walks that
tree in the order its productions completed, entering and leaving each
construction scope where the parser reported it, and binds names, assigns
types, registers constants and types, and reports diagnostics in source
order. A node whose kind depends on resolution (a name that reads a field,
constant or builtin; an element tag that names an object type) is morphed in
place at a size its syntax form reserved, so every parent keeps its link.
`bind` rebuilds the canonical scope/`NameEntry` graph from those retained
AST-facing facts, rejects any node the resolve pass did not reach, rewrites
declaration/use and capture edges to that graph, and only then publishes
`BOUND`. Post-reduction semantic validation remains manager-owned and a
rejected validation cannot publish `INDEXED`. The compiler-time record now
measures physical parse, build, bind, validate, and index stages; `bind_us`
is no longer fabricated as zero. Construction scopes and their declaration
lookup are private resolve-time support for bottom-up type assembly: the
physical binder replaces every published scope, declaration, use, and capture
edge with the canonical graph before `BOUND`; allocation failure rejects the
resolve pass without publishing a partial root. They are not a second binding
schedule or lowering authority, so the **D8.2.5** pass-manager requirement is
complete. This is an implementation-only status update; merely renaming a
pass would not satisfy **D8.2.5**.

Current end-to-end evidence for this status slice is `make test-lambda-baseline`
at `4,098/4,098`, including MIR emission/ratchet and forced-GC suites, and the
release Test262 target invocation at `40,261/40,261`, with zero
non-fully-passing, failed, regressed, or retried entries. Those gates verify
the physical Lambda binder and the unified schedule.

The post-P6 **D8.2.4** follow-up makes the direct lexical parent of every
source function an `AstIndex` fact keyed by `FunctionId`. The relation follows
the index's structural parent links, not `owner_functions` labels recovered
from source spans; class-field synthetic collection alone applies its explicit
direct-descendant policy to this shared relation. `JsFuncCollected` retains its
`FunctionId` and MIR-emission artifacts but no parent cache, and JavaScript
strictness/ancestor consumers resolve the common parent record and its shared
structural-descendant predicate. This is an
implementation-only status update; no formal ruling or document semver changes.

P3e (2026-08-28) remains the declaration-snapshot slice:
eval-preamble publication and batch/preamble declaration snapshots share
`js_preamble_entries_from_module_consts`, a single owned map-to-array copy with
partial-copy unwind. This retires the two duplicated snapshot walks without
changing the mode-specific module-variable policies.

P3f (2026-08-28) initially consolidated finalized MIR volume accounting into
one shared module walk under **D8.2.5**. The 2026-10-22 implementation follow-up
retires that walk: Lambda and LambdaJS reuse the shared emitter's existing
label-excluding function metadata. Lambda accumulates module/function/total
counts during emission and subtracts late prologue removals; link policy uses
the cached largest-function count, which can overestimate by four instructions
before that pruning. This changes accounting mechanics, not the ruling.

P1e (2026-08-29) completes the JavaScript side of the child-ownership slice:
core-shaped JS nodes now delegate to `ast_visit_core_children()`, while
`js_ast_children.cpp` retains only extension layouts. The adapter preserves
list/sibling order and the `do...while` body-before-test order, and the former
duplicated JS core rows are retired under **D8.2.4**. The current `master`
governed tree was 312,272 lines before this slice and is 312,260 afterward;
the historical 310,711-line project anchor is retained for audit, not
reinterpreted as a passing final cap.

P4a (2026-08-29) moves JavaScript strictness propagation into function
collection. The collector now publishes compilation-unit, direct-directive,
class-method, synthetic-field, descendant, and class-heritage strictness while
the owning ranges are available; the redundant post-collection propagation and
AST containment walk is retired. Class-method ownership (`is_class_method`) is
published at the same boundary. This is an implementation-only **D8.2.4**
slice; no formal semantic ruling or document semver changes.

P4b (2026-08-29) moves JavaScript direct-`eval` classification to the shared
AST-support owner. The core/extension child contract supplies one recursive
node scan with explicit nested-function/class boundaries, and the shared
function helper covers both parameter defaults and the body. MIR collection and
the AST interpreter now consume that helper; their duplicate scans are retired,
including the collector's former 96-line implementation. This is an
implementation-only **D8.2.4** slice; no formal semantic ruling or document
semver changes.

P4c (2026-08-29) moves JavaScript `arguments` observation to the same shared
AST-support owner. The function fact scans parameters and the body through the
core/extension child contract, retains lexical arrows, and excludes nested
ordinary functions, methods, and classes. MIR collection and the AST
interpreter consume that fact; the interpreter-local scan is retired while
MIR's reference set remains the capture-edge source. This is an
implementation-only **D8.2.4** slice; no formal semantic ruling or document
semver changes.

P4d (2026-08-29) moves AST tail-reuse eligibility to the shared AST-support
owner and centralizes nested function/method/class boundary handling for the
direct-eval, `arguments`, and tail-reuse scanners. The interpreter-local
structural scan is retired; `eval`, `with`, `try`, `arguments`, and nested
function safety conditions remain unchanged. This is an implementation-only
**D8.2.4** slice; no formal semantic ruling or document semver changes.

P4e (2026-08-29) moves `arguments`, `this`, and `new.target` observation to one
shared AST-support mask, including lexical-arrow propagation and `super`-based
`this` observation. The temporary arrow pseudo-reference walk and MIR-local
`with` scan are retired; the dynamic-environment boundary remains explicit.
This is an implementation-only **D8.2.4** slice; no formal semantic ruling or
document semver changes.

P4f (2026-08-29) retires the duplicate `FnAnalysis` embedded in
`JsFuncCollected`. The AST function node is now the sole owner of shared
function analysis consumed by JavaScript and the common runtime; MIR collection
metadata reaches it through one accessor. The capture analyzer's four repeated
self/lexical-pseudo-capture initializers are replaced by one shared initializer
in the same deletion-funded slice. MIR-only collection artifacts remain
temporarily on `JsFuncCollected` pending their indexed-table migration. This is
an implementation-only **D8.2.4** slice; no formal semantic ruling or document
semver changes.

P4g (2026-08-29) replaces the per-function recursive subtree walk used to
rediscover enclosing lexical declarations with the bound `NameScope` parent
chain. The projected facts retain lexical kind, source range, binding pointer,
and Annex-B declaration markers, so capture analysis consumes the scope graph
already published by the indexed compilation unit. The old collector and its
private child-dispatch context are deleted in the same **D8.2.4** slice. This is
an implementation-only change; no formal semantic ruling or document semver
changes.

P4h (2026-08-29) moves capture-array capacity and teardown to the AST-owned
`FnAnalysis` record. JavaScript collection entries retain only their temporary
backend view while indexed capture/environment work is in progress; the common
analysis record owns the allocation and capacity. P4i (2026-08-29) removes the
remaining JavaScript pointer/count mirror and routes all MIR capture access
through that AST-owned array/count. Class-method collection relies on the
publication pass to initialize `FnAnalysis` before capture facts are touched.
These are implementation-only **D8.2.4** slices; no formal semantic ruling or
document semver changes.

P4j (2026-08-29) moves JavaScript profile facts, parameter/return metadata,
capture storage, and scope-walk caches to AST-owned `FnAnalysis`. The former
recursive parameter and return inference walks are replaced by `AstIndex`
owner-filtered dispatch; capture propagation is a queued fixed point and
call-site evidence reads indexed call nodes. `JsFuncCollected` now retains
collection/backend artifacts only. This is an implementation-only
**D8.2.4** slice; no formal semantic ruling or document semver changes.

P3j (2026-08-29) makes the Lambda MIR Direct driver conditionally run the
shared indexed-AST prerequisite for retained pre-built AST callers, then keeps
const-fold, MIR lower/finalize, and link/entry in one ordered manager schedule.
P4k (2026-08-29) replaces the remaining recursive suspension, assignment,
boxed-return, body-local, constructor-shape, and body-reference scans with
indexed owner-filtered facts and hardens sparse scope-environment writeback by
skipping unused parent-env slots. These are implementation-only
**D8.2.4–D8.2.5** slices; no formal semantic ruling or document semver changes.
P4l (2026-08-29) then retires the duplicate lexical-key and declaration-shape
walks, reusing indexed scopes, the shared child-enumeration contract, and the
existing frame disposer. It also records the deliberate rejection of a generic
lexical-binding counter rewrite after sibling double-counting changed
Highlight.js behavior. This is an implementation-only **D8.2.4–D8.2.5**
slice; no formal semantic ruling or document semver changes.

| Ruling | Status |
|---|---|
| D4.4.1–D4.4.2 | **Native P2 profile implemented 2026-10-15.** One manifest generates compiler-checked bindings and Lambda declarations with matching complete revisions. Native record/scalar spans validate counts, capacity and stride, share invalidation controls and precisely trace record Item edges during publication and collection. Structural reads, enumeration, validation and formatting reject stale views. Buffer counted arrays, offset references and general cyclic schemas remain separate extensions. [PDF P2 progress](Lambda_Impl_PDF_Output_Phase2.md#11-p2-native-reference-binding-progress-2026-10-15). |
| D2.1.5 | Guardrail layer partial: ~24 raw `>> 56` sites across 11 files, open-coded `get_double` derefs, raw `MIR_EQ` emissions outstanding. |
| D2.2.5 | **Implemented 2026-10-07 (USER).** MVP-Lmd preserves finite INT Items through storage and boxed calls, classifies INT/FLOAT as JS Number, and uses range-proven native integer regions plus checked boxed add/subtract/remainder. Widening retains JS binary64 rounding and signed zero. Shared finite boxing/unboxing/band emitters also serve Lambda; no runtime helper was added. Focused tests pass normally and under forced GC; the shared-emitter baseline passes 6284/6284 and the unchanged Test262 baseline 40261/40261. Archived release evidence and conservative proof limits: [JS_MVP_Lmd §9.6](../jube/JS_MVP_Lmd.md#96-archived-integer-evidence--2026-10-07). |
| D2.3.3 | **Met by admission, not as written; ruling text awaits the user (re-checked 2026-10-23).** The blind-cast crash is closed in MIR Direct: a declared container parameter is admitted at the `_b` boundary (DF2) before any pointer cast; a static mismatch is E207 at compile time and a dynamic null or wrong kind raises E201 (DF4). `p2it` landed as specified. `it2map`/`it2list`/`it2elmt`/`it2obj`/`it2arr`/`it2range`/`it2path`/`it2p` exist only as unchecked casts (`lambda/lambda.h`, `lambda/lambda.hpp`). The ruling's "NULL on mismatch (the `it2s` contract)" is therefore not implemented and conflicts with DF4's raise; the earlier note that the MIR path keeps container params as ANY predates the pointer lanes of D2.4.1. Record: `vibe/impl/Lambda_Box_Unbox2 (retired).md`. |
| D3.3.1 | The append-site split is landed and the `disable_string_merging` flag it replaced has been retired from both context structs. **Landed with [List Fixes](<Lambda_List_Fixes (done).md>) P1–P4 (2026-09-22/23, both tiers); that plan closed at P6.** One kind bit (`is_spreadable`; the old `is_content` survives only as LambdaJS's `is_js_arguments`); a value-decided sequence append (`array_push` splices a list by its bit; array literals, pipe items included, never choose spreading syntactically); the verbatim append `array_push_verbatim` at every positional site — argument and rest lists, group/join/order keys and rows, path keys, query and `find` matches, directory listings, clones, element-copying transforms, `zip` pairs; single-value slots store a list's array image (`slot_image`, S2.5.6); and the content append now applies all of S2.6.2–S2.6.4 unconditionally — a lone `""` is dropped, adjacent binaries merge, and the `input_context` gate on string merging (the retired flag in disguise) is gone. Content writes stay normalized (S2.6.5): `e[i] = v` rebuilds the children through the content append and `push` on an element appends content. `input-ics.cpp` builds its component sequence with the pool-owned verbatim `array_append`; `input-mark.cpp`'s `list_push` is element content, the content append's own case. P4 removes the last syntactic spreading: `*x` no longer marks its operand (the retired `item_spread`) but builds the list of x's items, which the sequence and content appends then splice by its kind bit like any other list, so the ruling's "decided by the value, never by the syntax" now holds for the spread operator too. The LaTeX deviation closed on 2026-10-02: element appends normalize strings, command argument runs are array children, and the LaTeX package and formatter consume their entries positionally. The unused LaTeX AST-normalization module and reconstruction helper are retired. MarkEditor child writes normalize in both edit modes (54/54 editor tests); explicit web DOM node APIs retain node identity (112/112 node/range tests). Verified: Lambda baseline 6151/6151, including MathLive 921/921; MarkBuilder/deep-copy 121/121. [Input_Latex](../input/Input_Latex.md). |
| D2.3.4–D2.3.5 | L0–L4 first slice landed 2026-08-28: explicit `INT_LANE`/machine reps, full-contract `MirValue`, canonical contract mapping, fail-closed carrier router, direct transition/fail-closed fixtures, and migration of arithmetic, branch, binding, index, call, and return consumers. Semantic `MIR_reg_type()` probes are removed from Lambda expression lowering. The 2026-08-31 P5 follow-up makes `transpile_primary_value()` publish literal/primary `MirValue` descriptors directly and retires its raw dispatcher arm. **Implemented boundary audit 2026-09-05:** `transpile_expr_value_core()`/`transpile_expr_value()` and `jm_transpile_expression_direct()`/`jm_transpile_expression_value()` now form the respective core demand-driven `MirValue` boundaries; no core `transpile_expr*` or `jm_transpile_expression*` function returns `MIR_reg_t`. Internal physical-register helpers remain below the boundary. |
| D2.4.1 | Nullable-lane first slice landed 2026-08-05 (LaneStorageDesc, native arrays, packed nullable fields, scalar ABI); `f16?`/`f32?`, JS IC lowering, mutable ArrayNum views, vector/N-D kernels pending. |
| D2.4.2, D3.2.1, D2.6.2 | **Implemented 2026-09-14.** `i64?`/`u64?` native Arrays and packed Map/Shape fields use descriptor-selected, destination-owned `TypedItem` slots. Construction, mutation, static materialization, rebuilding, COW, reads, and GC tracing preserve the selected layout; the regression covers JIT/interpreter plus forced-GC number-frame reuse. |
| D3.2.2 | ArrayNum `==` representation-sensitivity is a known live bug (also gates the data-processing engines). |
| D3.2.1 | ELEM_INT i64 revert landed; SIMD kernels only partly re-enabled (C16-era gating comments remain). |
| D3.1.5, D3.1.1 | **Ruled 2026-09-03 (USER), not implemented** — both belong to phase 2 of D3.1.1. Shipped state is the v2 description below. |
| D3.1.5 (MVP slice) | **Implemented 2026-10-07 for the bounded MVP:** plain JS objects and ES Map carry distinct nominal records on ordinary Lambda Map/TypeMap storage. Add, retype and delete preserve those records. No VMap or full-JS property/lifecycle hook participates. This does not claim the full engine's nominal migration or D3.1.1 is complete. Scope and evidence: [JS_MVP_Lmd §10](../jube/JS_MVP_Lmd.md#10-map-and-plain-object-phase). |
| D3.1.5 (shipped state) | **Conformant as of 2026-09-03**, verified rather than assumed: `js_new_object` (`js_runtime.cpp`) allocates `LMD_TYPE_MAP`; a sweep of `lambda/js/` and `lambda/dom/` finds no construction of an `LMD_TYPE_OBJECT` value. The inbound-acceptance sites pass whole Items to shared helpers rather than punning, except `js_props_storage_map`, which returned `object.map` for a nominal object and was rewritten to return the packed buffer and capacity; the Map-only extension table is now gated to maps. JS suite 360/360, coercion 15/15, baseline 4083/4083. |
| D3.1.1 (phase 1, layout) | **Implemented 2026-09-03.** `Map` is the base; `List`/`Array` and `ArrayNum` extend it; `Element` extends `List` and declares no fields of its own. The attribute face is at one offset in every container, so the kind-aware accessors and per-kind JIT offset helpers this file's v1 row describes are **retired**: `mir_container_data_offset`/`mir_container_type_offset` are gone and codegen uses `offsetof`-derived constants. The GC traces and compacts every container through named `GC_OFF_*` offsets. Sizes: `Map` 32, `List`/`ArrayNum`/`Element` 64. Layout is pinned by static asserts in **both** `lambda.hpp` **and** the C mirror in `lambda.h` — the mirror had none before, and adding them exposed a pre-existing divergence: it declared a 2-byte `flags` where `Container` uses single bytes, so the mirror never matched the real header ([LR03-9](../Lambda_Issue_Ledger.md)). Two GC gaps the new layout opened were closed: an array's and an ArrayNum's attribute buffer are now traced AND compacted (`gc_compact_attr_face`). **The reserved-tail JS-properties slot is retired**: a JS array's companion map now lives in the array's own attribute face (one tagged Item under `ArrayPropsShape`), which deletes the `extra` reservation, the tail-shift that had to rebase embedded scalars to make room, the companion carry-over in numeric growth and in tagged promotion, and the two GC tail-slot marks. The companion stays a real object — a sparse array's is a `SparseArrayMap` with its own fields — so what moved inline is the pointer. `has_js_props` is gone and its bit is reserved as `CONTAINER_FLAG_NOMINAL_RESERVED` for D3.1.1. Baseline 4083/4083, JS 360/360, GC stress 93/93, tier parity, stable under forced GC. Per USER 2026-09-03 the array-header growth needs no benchmark gate: this is the container layout. |
| D3.1.1 | **Implemented 2026-09-25 ([Impl_Element_Type_Sharing P0](Lambda_Impl_Element_Type_Sharing.md)).** `TypeElmt::content_length` is gone: a declared type's content section resolves to `content_list`, a typed `TypeList` whose slots are filled as a bracket pattern's are (`fill_sequence_pattern_slots`), and every instance and literal type leaves it NULL — the parsers' 120 per-element count writes, `MarkEditor`'s type writes on content edits and its new type per content copy are gone. An element literal's content count comes from its syntax tree, keeping `list_fill` pre-sizing. `TypeNominal::content_length` is removed; the nominal kind follows `content_list` and a derived type inherits both from its base (S2.1.3v2). Baseline 5869/5869; validator GTests 58/58, 38/38, 44/44. |
| D3.1.1 (phase 2, nominal) | **Implemented 2026-09-03, apart from open instances.** Nominal-ness is now a property of the type descriptor: `TypeNominal` is the record, allocated once per declaration, pointed to by `TypeMap::nominal`, with `Type::is_nominal` as the base-flag discriminator — it lives on the BASE because the question must be safe to ask of a bare singleton `Type`, which shares the map tag but is not a shape. **The representation flip is done**: a nominal type and every value built from it wear the DECLARED structural kind, so `type P { x: int }` yields maps and `type B { …, string* }` yields elements, and `is object` / `is map` are independent axes exactly as S2.1.1v3 says. Nominal sameness is record identity, not name equality (S5.4.2v3), and equality gates on it, so a structural map never equals a nominal one. The object shape extends `TypeElmt`, so a nominal element's `name`/`content_length`/`ns` sit where every element path reads them. Rerouted onto the record: type matching, `fn_is`, equality, the total order, `fn_type`, `fn_name`/`fn_name_from_type`, member and method resolution in both lanes, printing (a nominal map prints `<P x: 3>`, not braces), the formatter dispatch, and `fn_len` — including the JIT's `fn_len_e` specialization, which returned content only and was the one place the two tiers disagreed. Every construction path now carries the real kind; only the `TYPE_OBJECT` singleton still mentions the tag, and the enum member is vestigial. Fixture `test/lambda/object_nominal.ls`; baseline 4083/4083, JS 360/360, GC stress 93/93, exact tier parity, stable under forced GC. **S2.1.4 part 3 now holds too**: an unknown member write GROWS the shape instead of failing. Growth already existed in `map_extend_open_shape` but was reachable only from the cow-path caller, which is why an ordinary `p.z = 9` silently did nothing on T0 and errored on MIR; it is now the shared miss path in `fn_map_set`, and it admits element-shaped values as well — valid only because D3.1.1 put every container's attribute face at one offset. The grown shape carries the nominal record forward, so the value stays an instance of its type with its methods intact, which closes [LR03-8](../Lambda_Issue_Ledger.md) at the one site that actually produces a grown shape. The declared prefix keeps its layout, satisfying D4.2.5; the grown shape drops `is_trusted_contract`, so direct access falls back to the checked path. Fixture `test/lambda/proc/object_open_instance.ls`. **The dead `LMD_TYPE_OBJECT` arms are swept**: 251 mentions down to 68, all of which are live or inert — the `object` TYPE itself (`TYPE_OBJECT`, `LIT_TYPE_OBJECT`, `type_info`, `get_type_name`), the range checks that use the tag as the upper bound of the contiguous container band, and tag-keyed template machinery (`ItemTagToType`, `IsMapLike`, `item_payload`) that no value can now reach. The enum member is documented at its declaration as type-only. **The member is now removed** (2026-09-03, on the ruling that the retirement be carried through): `object` is a TYPE, not a tag, so the `TYPE_OBJECT` singleton wears the map tag purely to route through the container arms of the type switches and is matched by POINTER identity everywhere. Removal shifted every later TypeId down by one — MAP=19, ELEMENT=21, TYPE=22, FUNC=23, ANY=24, ERROR=25 — which is an ABI change only for artifacts that pin a raw tag: the two JS error-lane MIR goldens pinned `LMD_TYPE_ERROR` as the literal 26 and were re-pinned to 25 with the constant named in their description. The shift also exposed the one real hazard of giving `object` no tag of its own, and it is worth stating because it will recur for any future tag-sharing type: `lambda_type_node_singleton` (`runtime/ast.hpp`) resolves a type node to its runtime identity and falls back to the node's TAG when no singleton arm matches, so `object` silently resolved to the `map` singleton and `{x: 1} is object` answered true on BOTH tiers. The helper already carried this arm for `date`/`time`/`list`/`number`/`integer` for exactly the same reason; `object` now has one too. `fn_is` likewise matches `&TYPE_OBJECT` by identity ahead of any tag comparison. Baseline 4085/4085, JS MIR emission 21/21. The sweep also surfaced one real regression the flip had left: `total_type_rank` selected the object ORDER BAND by container tag, which nominal values no longer wear, so they sorted with plain maps and elements instead of between them; both it and the `total_cmp` nominal arm are now keyed on the record, and `test/lambda/object_nominal.ls` pins the band. Baseline 4085/4085, JS 360/360, GC stress 93/93. |
| D3.1.2 | **Ruled 2026-09-03 (USER), not implemented.** It depends on the phase-2 representation flip. |
| D3.1.1 (v1, shipped) | **Implemented 2026-09-03; superseded by v2 above.** `Object` is a typedef of `struct Element`; the GC traces and compacts it on the element path; `TypeObject::content_length`, `lambda_content_list`/`lambda_content_count`, `lambda_attr_shape`/`lambda_attr_data`, `is_element_family_type_id`, `object_content`/`object_content_fill`/`object_content_fill_items`, the T0 and MIR literal content phases, and the content-face admissions in `fn_len`, `item_keys`, `iter_len`/`iter_key_at`/`iter_val_at`, `item_at`, `fn_index`, `fn_eq`/`object_eq`, `total_cmp`, `in`, both clone paths, the arena deep copy, `print_tagged`, and the formatters. De-punned: `as_map(ItemOf<LMD_TYPE_OBJECT>)` is deleted, `MapReader` holds shape+buffer instead of a `Map*`, `ElementReader` resolves the tag by kind, `js_props` returns the buffer instead of a `Map*`, and `mir_container_data_offset`/`mir_container_type_offset` select the JIT's packed-field offsets per kind. Fixture `test/lambda/object_content.ls`; baseline 4083/4083, exact tier parity, stable under `LAMBDA_GC_FORCE_EVERY`. Not implemented: schema-driven input producing objects — verified 2026-09-03, `input(doc, {'schema': P})` yields a plain map, because the option drives a post-parse `lambda_type_check` rather than construction. The validator's content-arity check against `content_length` is NOT missing after all: `validate_against_element_type` enforces it and an element-kinded nominal type reaches that arm by tag. It is now run rather than code-only: `lambda-runtime-full` carries the direct type-pattern reducer and its schema loader registers the direct parser's canonical type declarators, so the enabled validator tests complete and the CLI reaches genuine validation. The shipped pair presently fails its independent `element`-versus-`map` check; LR13-3's root-selection contract and LR13-9's missing baseline gate remain open. |
| D3.1.1 (v1 validator verification update, 2026-09-05) | This amends the prior row's stale loader finding: all ten `lambda-runtime-full` validator test executables load and enumerate their GTests after their hosts retain the DSO's two core-global imports. The post-startup null reduction target is fixed by linking the direct type-pattern parser, and the loader recognizes direct `AstDeclaratorNode` aliases, so selected root lookup proceeds to validation. This verifies neither schema-driven object construction nor a root-selection policy: the former still returns structural maps, and the latter remains LR13-3 ([LR13-9](../Lambda_Issue_Ledger.md#lr13-9)). |
| D3.1.4 | Bound `fn` methods already build the receiver-captured closure (`to_closure_named` in `lambda-eval.cpp`; `closure_env` holds the receiver). Not verified: the compile-time rejection of a bare `pn` method reference on both tiers. |
| D3.1.3 | No identity carrier exists; nothing to verify until DO25 is decided. |
| D6.2.1 | v2 (companion-lane entries) decided 2026-08-14, not implemented — the v1 trailing-home wrapper ABI ships until Return_Value P4. Ownerless-slot GC scalar fallback active and counted; removal gated on the per-boundary inventory reaching zero. SG2 OQ audits open (dispatch-helper enumeration, resume-path slot reads, RetItem census). |
| D2.5.3, D4.2.3, D4.2.4 | **Core implemented 2026-11-02; aggregate qualification in progress.** Deferred checks retain boxed outcomes through continued execution, whole-variable writes, branch/loop joins, immutable aliases, captures and borrow publication. The original annotation alone is not a clean-value proof. Deferred return checks retain their call outcome, and tail-pair forwarding cannot bypass admission. Native initializer reuse and borrowed-return planning use the same effective outcome facts. Focused tier/GC and native diagnostic checks pass; package/demo qualification is pending. [TE-18.1–TE-18.2 implementation status](../Lambda_Design_Type_Error_Handling.md#reassignment-outcomes). |
| D2.5.2 (prior lane-gating qualification) | **Prior lane-gating implementation, 2026-09-27 (LR12-24), on the D7.1.3 fact; this predates D2.5.3's continued initializer outcomes.** Lane entry is gated on static proof: a call whose value may be an error it does not declare (a defect-capable callee, a call through a function-type contract, a system function that rejects or passes through an operand that may be one) is a boxed join to every lane planner, and so are a member, element or arithmetic result of one; a literal slot fed by one is boxed on both tiers. Exact lowerings keep the common consumers native: equality against an in-band int compares Item words, integer arithmetic tests the first error and runs on the lanes, and a destination whose contract the join's success already satisfies tests only the error arm. The 2026-09-18 routing (shape 4 for a body that originates a defect) now covers every origination class. Cost: +14/+82/+217/+53 module instructions on the four affected ratchet probes (tune4, cube3d, deltablue, prettier_ast); runtime (release, paired A/B against the base, 15 pairs, 27 typed rows): 20 rows within ±2%, three faster (prettier_ast −3.8%), and deltablue +2.4%, fast_diff +4.8%, hashmap +8.4% (−4.5% to +15.6% across four runs) and queens +12% (a 70 µs run) slower. The remaining cost is join arithmetic feeding a declared accumulator. Known violation V1 is unchanged: `fn_array_set` silently despecializes a declared `int[]`, which keeps D2.5.1 true but makes the S7.7.2 dominance guarantee false. ([LR12-24](../Lambda_Issue_Ledger.md#lr12-24)) |
| D10.1.1–D10.1.2 | **Implemented 2026-10-02.** Twenty fixtures migrated with unchanged contents; Lambda/input baseline 6,162/6,162, MIME detection 13/13, new extension regression matches on interp/jit/auto, and 15 CLI checks pass. Verification and existing fixture residue: [Input_System appendix](../input/Input_System.md#appendix--mark-extension-migration-2026-10-02). |
| D10.1.3 | **Implemented 2026-10-07.** Format `slides` parses with the Mark parser (`input.cpp`); the `slides` document transform (`lambda.slide.present.present`) backs the `.slides` view/layout route; no filename detection was added. The three demo decks were converted from `.ls` and re-format byte-identically to their generator output; five slide UI fixtures open `.slides` decks. Record: [Lambda_Pkg_Slide_Presentation §0](../vibe/Lambda_Pkg_Slide_Presentation.md). |
| D10.1.4 | **Implemented 2026-10-15.** Shared XML emission profile, explicit format and MIME/extension routes, direct Radiant loading, and Lambda-returned document unwrapping. XHTML golden passes on interpreter, automatic and MIR tiers; five Radiant integration tests cover HTML parity, linked/inline CSS, foreign content, scripts, HTTP MIME/redirects, no-doctype standards mode and the feature gallery. The gallery's 27 checks and nine replay assertions pass in desktop/narrow interactive views; static layout/render retains a startup computed-border discrepancy. MIME tests 13/13 and related Lambda XML/HTML/parse goldens 26/26 pass. Full Lambda/Radiant baselines and the native parser test runner cannot link with the concurrent Wiki change's unresolved `array_push`; those gates remain unverified. Record: [Input_XML §XHTML admission](../input/Input_XML.md#xhtml-admission). **v2 catalog extension implemented 2026-10-26:** recognized public IDs select the shared HTML entity table without external DTD acquisition; seven native XHTML parser tests and the combined Lambda/input baseline (8,251/8,251) pass. The Radiant baseline passes. The broader input-processing run completes with 4,533/4,559 passing; remaining wiki/prose-format and PDF checks are recorded in [the writing-modes implementation log](Radiant_Impl_Writing_Modes_WPT.md). |
| D10.1.5 | **Implemented 2026-10-18 for the current HTML SVG inventory.** Shared current-node namespace classification preserves SVG integration-point closing tags and HTML descendant recovery. Parser and independent Chromium controls are recorded in the [SVG parser record](Radiant_Impl_SVG_Support.md#12-shared-svg-parser-implementation-record); full standards conformance is not inferred. |
| D10.1.5 | **Implemented 2026-10-18.** External picture intake delegates to the HTML5 parser and retains its `#document` owner; isolated DOMs use HTML name/namespace interpretation. FO serializes namespace-resolved graphics through canonical HTML-compatible SVG names and the existing fragment formatter. Explicit XML/XHTML input remains unchanged. Qualification is recorded in the [SVG parser record](Radiant_Impl_SVG_Support.md#12-shared-svg-parser-implementation-record). |
| D10.1.6–D10.1.7 | **2026-10-18 implementation.** Current-DOM saves and frozen CSS/SMIL projections reuse `dom_serialize_xml`. Reopening preserves edited content, independent local/file-backed use states, sibling filter definitions and a frozen local SVG image, at both densities/cache modes with independent Chromium checks. Capture preserves the live source/time. The retained-fragment shutdown use-after-free is fixed under D5.2.1; scoped ASan passes with leak detection disabled. **D10.1.7 remains partial:** nested resource closure, image imports within use templates and broader host inheritance combinations remain unqualified; Linux/Windows runtime and aggregate qualification retain separate status. See [SVG support §14](Radiant_Impl_SVG_Support.md#14-outstanding-work-follow-up). |
| D4.1.1 | Binary schemas use `TYPE_KIND_BINARY_SCHEMA` under the existing Type tag (2026-10-15); imported declaration values retain descriptor identity across module type lists. The general `Type*` kind-discrimination is code-authoritative only — no design record owns the first-class type-value representation (DO22); the type-graph de-pointering census is deferred to its own doc (CP §6 census C). |
| D4.2.3 | Constrained-type enforcement is base-only; the `is`/`fn_is`/validator three-way divergence is open (TE-6 P5). |
| D3.4.3 | **Implemented 2026-09-25 ([Impl_Element_Type_Sharing](Lambda_Impl_Element_Type_Sharing.md) P0–P3).** Maps built through `map_put` — every `MarkBuilder` parser and every JS object — share through the per-`Input` tree (`map_put_with_data_growth`): the edge table dates from 2026-06-25, the shared root and the bounds (256 edges from the root, 16 from any other type, 1,024 map types per `Input`) from 2026-09-09; maps stopped interning through the shape pool on 2026-08-08. Parsed elements share since P1: `ElementBuilder` starts each element on its tag's root (`elmt_tree_root`) and `putToElement` adds attributes through the tree (`elmt_put_tree`), under their own budget of 16,384 per `Input`. Editor rebuilds since P2 replay their field list through the tree (`type_tree_root_like`, `type_tree_follow`), editing in place only a type the container owns — the old guard had also rewritten tree and literal types, re-laying a sibling map's fields. P3 deleted the shape pool (`shape_pool.cpp`, `Input::shape_pool`, `elmt_finalize_shape`, `map_finalize_shape`, `shape_builder_finalize`). Verification: peak RSS on a 13 MiB layout-test corpus (203K elements, 1,775 types) 184.0 → 120.2 MB and on a 13 MiB page corpus 109.6 → 85.8 MB, parse 1.42× and 1.39× faster; 17,230 HTML files, 99 test inputs and 84 formatter outputs identical to the pre-change build; the map budget took the test262 batch peak from 5,662 to 891 MB (2026-09-09). Since 2026-09-25 tree nodes and edges come from the `Input`'s arena and go with the `Input` (D5.1.4, D5.2.5; `input_tree_alloc`); the element-root table stays a pool block and frees its old copy when it grows; JavaScript descriptor retags (`js_property_attrs.cpp`) keep `js_input`'s pool. **v3 implemented 2026-09-25** (`transition_target_for_key`): a child extends its parent's chain in place while the parent's `last` has no successor, and takes the parent's lookup table while it has room and holds no entry with the added field's identity (`typemap_hash_holds_equal`); an entry the tree appends records its position (`ShapeEntry::chain_index`, in padding) and `typemap_hash_entry_is_own` rejects a descendant's; an in-place child shares the parent's `slot_entries`. Every walk goes through `FOR_EACH_MAP_FIELD` / `typemap_next_field` / `shape_chain_next_until` — the link is renamed `ShapeEntry::chain_next`, 181 walks were converted, and lint `no-raw-shape-chain-walk` flags a raw read — and the collector stops at `last` (`LAMBDA_GC_OFF_TYPE_MAP_LAST`). Verification (release, medians of five): one 1,000-key JSON object's parse peak 94.1 → 16.5 MB (debug: 98.8 → 17.4 MB; a 1,000-key JS object 84.7 → 18.4 MB); 13 MiB HTML corpora (real-site pages 99.3 → 99.0 MB, layout tests 104.7 → 104.5 MB) and 60K 20-field JSON records (102.7 MB) unchanged in memory and parse time; Lambda baseline 5,924/5,924, Radiant baseline green, test262 baseline 40,261/40,261 with an unchanged batch peak (643.9 vs 639.8 MB); `TransitionTreePrefixSharingTest` (6). **v4 implemented 2026-10-05** ([LR03-38](<../Lambda_Issue_Ledger (fixed).md#lr03-38>)): `map_extend_open_shape` first tries `map_extend_via_runtime_tree`, which holds the tree `Input` as a context capsule extension (`RUNTIME_SHAPE_TREE_OWNER`, so `EvalContext`'s layout is unchanged), tests node membership by `type_index` against that `Input`'s type list (`type_tree_owns`), adds through `type_tree_add_map_field`, and grows the map's data by doubling. `fn_map_set` answers a plain string key from the type's hash table, hit or miss, instead of walking every field. Building a 512-key map 512 times went from 9.8 s and 11.9 GB to linear time (64 builds of 1,024 keys in 11 ms, debug); fixture `proc/map_runtime_shape_tree.ls`, in the forced-collection sweep. **v5 implemented 2026-10-06** ([Impl_Map_Transition_Coverage](Lambda_Impl_Map_Transition_Coverage.md) P0–P3): the runtime tree keeps external-parent edges in a table keyed by a structural fingerprint of the parent (`external_parent_edges`), mints children with the parent's names copied into its arena (`clone_shape_entries_owned`, `copy_external_identity`), and reuses a child only when `external_child_matches_parent` finds every copied value equal to the live parent's; retype edges (`type_tree_retype_field`) share the target of a type-changing write, whose data move `container_move_to_type` serves the private rebuild too; `runtime_result_shape`, group-by groups and join tuples start on the tree; the tree is dropped with its heap generation (`runtime_shape_tree_release`, [LR03-43](<../Lambda_Issue_Ledger (fixed).md#lr03-43>)). Lambda `Symbol` keys and JS identity keys take edges (D3.4.4). Debug peak RSS for 16 builds of a 1,024-key map: from a literal 1.43 GB → 33 MB, from a parsed object 1.43 GB → 34 MB (16 documents of one shape share one path), from a spread 1.43 GB → 33 MB. Lambda baseline green but the environmental `pdf_svg_page_resources`; test262 with no semantic regression (three unicode-identifier cases flag batch time under load; a paired A/B measured no difference). **Residue:** a declined add (fan-out 16/256 or the budget; P1.5 measured both caps on 268 benchmarks and the Lambda corpus and kept them, since nothing declines below the root and a 2,048-edge root cost 9-10% CPU in linear edge walks), JavaScript shapes, spread link-slot types, qualified keys (LR03-40) keep private types. **Admission reuse completed 2026-10-09:** `type_tree_retype_contract` shares structural admission layouts through the same bounded retype edges; the edge compares the full field contract pointer, rather than its coarse TypeId, so `number`, `any` and distinct union contracts cannot alias. A declined edge retains the private rebuild. `TransitionTreeRetypeTest.FullAdmissionContractsShareBoundedLayoutsWithoutAliasingTheirTypeId` checks preserved extra fields, immutable parents, independent contracts, owner lifetime and 4,096 repeated admissions without arena/type-list growth (D3.4.5, D5.5.1). |
| D3.4.4 | **Ruled 2026-10-06 (USER, with S8.2.2v4; the JavaScript namespace mapping accepted the same day); not yet built.** Every read conforms (byte confirmation at each map and element access; identity kinds by record). The transition trees' edge match (`transition_target_for_key`), `typemap_shape_entries_equal`/`typemap_hash_holds_equal` and `map_existing_shape_entry` still take a differing id as a different name, so one spelling has one edge per name pool and a pooled key never shares an edge with an unpooled spelling; and the `property_key_requires_identity` gates at `type_tree_add_map_field`, `map_put_with_data_growth`, `elmt_put_tree` and `type_tree_follow` keep `SYMBOL`/`PRIVATE` keys off every tree although their edges already compare `(name_id, key_kind)`. Plan: [Impl_Map_Transition_Coverage](Lambda_Impl_Map_Transition_Coverage.md) P0; design record: [Design_Name_Identity](../Lambda_Design_Name_Identity.md) NI17, NI18. Unruled: the interop spelling of a JavaScript key seen from Lambda (`js.symbol.for.k`). |
| D5.1.4 | **v5 implemented 2026-10-02:** `arena_free`, `arena_realloc` and the arena free list are removed; `arena_retire` keeps a block on the arena's retired list (binned by span, coalesced, rejoining the tail when adjacent to it) and `arena_alloc` reuses it; its only caller is DOM node retirement (`dom_retire_recycle_one`). `arena_mark`/`arena_rewind` landed; ScratchArena rewinds its backing arena on scope end, restore and release, and registers as the arena's sole owner — a second owner, a direct `arena_alloc`, or an `arena_retire` on an owned arena asserts, and `arena_rewind` asserts on an arena holding retired blocks. Verification: layout view trees identical to the prior binary for 10,331 layout test files; `ArenaRetireTest` (7), `ArenaRewindTest` (4), `ArenaScopeOwnerTest` (2). Still pending: the every-arena audit for sites other than DOM retirement that would need retention. Pool v2 core landed in Mem_Heap R7 on 2026-08-11: `pool_alloc` carves user bytes from Pool-owned growth extents, uses boundary-tagged blocks and segregated free lists, and no longer calls `mem_alloc_loc`/`mem_free_loc` per user allocation or uses a pointer index. Growth starts at an optional reservation clamped to 1 KiB and rounded up to `1 KiB * 2^n`, doubles subsequent reservations, uses the context/memtrack path below 4 KiB and page-backed VM at/above 4 KiB, and commits at least `max(4 KiB, required block bytes)` page-rounded. |
| D5.2.1 | MemContext owns allocator identity/lifecycle; hardened memtrack and the VM region provider are the normative system-allocation substrate. Pool is a context-owned VM-extent allocator; its user blocks are not `memtrack_pool_*` allocations. The Pool v2 core migration is landed and verified. **2026-10-18:** cascade teardown follows explicit backing-child edges across contexts and reparenting; subtree unregistration needs no temporary allocation. Seventeen context and seven factory cases pass under scoped ASan; the retained-fragment navigation/export reproducer also passes. The broader every-allocator context-binding audit remains pending. |
| D5.2.2 | Stage 2 page allocation and the single MemContext failure coordinator are implemented as the allocator-retirement foundation. Full cost-based reclaimers remain follow-up work. |
| D5.1.5 | String-builder two-case model matches the code (`StrBuf` standalone; `StringBuf` owner-backed, pool-backed `realloc` growth). The arena-backed tail-growth variant does not exist yet — it needs the `arena_mark`/`arena_rewind` promotion of D5.1.4 (Mem_Heap R6). Formatters are the first consumer (already structurally ready: destination pool passed in, scratch on a separate pool, no destination allocations during the walk); parsers follow, subject to the tail-ownership hazard. NamePool still takes a `Pool*` — conversion is Mem_Heap R4b. |
| D5.2.1 | MemContext graph exists. Every `Input`'s arena and name pool are bound to a per-`Input` context since 2026-09-25 (D5.2.5); the every-arena/pool context-binding audit (Mem_Heap R6) has not run. |
| D5.2.3 | Ref-counting not implemented; shared-allocator census (Mem_Heap §15 Q7) pending. |
| D5.2.4 | R1a landed 2026-08-10 (release default `MEMTRACK_MODE_OFF`, measured: primes2 825→155 ms). R3a removed the historical Pool side index, and R7 landed the Pool-owned boundary-tag/free-list hot path with no global mutex or registry call on block operations. The 2026-09-17 revision makes the application-level ban on direct libc allocation explicit; STATS counters-only (R2), the `stack_alloc` split (MP-18), and independent release-performance evidence remain pending. |
| D5.2.5 | **Implemented 2026-09-25.** `pool_add_cleanup` (`lib/mempool`) runs a pool's cleanups newest first on destroy, drain and reset, before any block is released. Every `Input` has a context (`input.doc` with a URL, `input.local` without) created under its pool's context (`mem_node_owner`), and registers `input_pool_cleanup`; inside a cascade the cleanup skips the registry (`mem_context_in_teardown`). Before this, a URL-less `Input`'s arena stayed registered in the root context until exit — the owner it lost when arenas stopped drawing chunks from their pool (2026-08-08). The audit of every creator fixed three places that relied on that leak: npm `package.json` Items kept past their parse pool, the per-thread sysinfo cache reusing a destroyed pool's `Input`, and Script loading owning the direct parser's registries twice. Verification: Lambda baseline 5,903/5,903, Radiant baseline (all required), test262 40,261/40,261; `PoolCleanupTest` (6) and `InputLifetimeTest` (3), whose 64 created-and-destroyed URL-less `Input`s leave the allocator count unchanged; `NpmPackageJsonTest` (3) and `SysInfoRuntimeTest` (1) pin the two leak-dependent fixes and fail with them undone. |
| D5.3.1 | GC size classes and data-zone policy are retained; backing storage is owned by MemVmRegion and released by the owning GC heap. |
| D5.3.3 | Decided 2026-09-07: `gc_trace_shape_field` marks a `null`-typed lane's word conservatively (`gc_mark_possible_item`). Found through DO30: the auto tier's fast splay figure came from collections that freed the linked right subtrees (17k of 420k objects traced); reproduced deterministically on every tier with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`; pin `test/mir/lambda/gc_splay_null_lane` under the forced-collection stress sweep. The companion robustness fix keeps `fn_map_set`'s same-width retag from typing a shared literal shape's field as `error`. |
| D5.5.1 | Radiant and Lambda keep distinct policies over memtrack/VM ownership; legacy Pool/Arena backend wording is superseded; v3 recorded batch-only arena lifetime (two variants, D5.1.4); v4 (2026-10-02) admits retention for reuse for DOM node retirement and gives each scratch stack its own arena: `view_tree.scratch_arena` (layout), `view_tree.render_scratch_arena`, `view_tree.display_list_arena`, `view_tree.layout_pass_arena` (counter state and generated content), one arena per retained display-list fragment, and `OffscreenRenderArenas` for offscreen SVG and effect renders. |
| D5.5.2 | Ruled 2026-10-06 (user). **Partially implemented 2026-10-06:** the ownership lattice has no GC→Pool edge (a pool-domain field rejects `GcPtr`, probed in `test_own_tagged`), and built-in literals carry a static label. Audit: the JS DOM and `lambda/dom` setters copy into the document's Input arena or pool. Rooted document-side references (history state, `FileList`, Range/Selection wrappers, editing session root, custom-paint content, non-string UI attribute values) fall under the exemption (user, 2026-10-06). UI-mode script results comply since 2026-10-06: `list_push` deep-copies every non-string content value not already in the result arena into the result `Input` (pinned by `RadiantViewTest.UiScriptContentSurvivesForcedGc` under forced, poisoning GC). The Input-owned append (`list_push_with_owner`) and `MarkEditor` imports apply the same rule through one shared helper set. Live residue: values that reach the arena tree outside any append (a GC top-level script result, the math package splice in `cmd_layout.cpp`). Detail: `vibe/Memory_Safety_Template.md` §12.5 item 18. |
| D5.4.3 | COW Stage 1 landed 2026-07-23. Place-home publication and numeric-view snapshots were completed under D5.4.6 / S9.2.2v2 on 2026-11-03; other Stage 2 checks retain their separate status. |
| D5.4.6 | Implemented 2026-11-03 on interp, AUTO and JIT, including scalar places, partial errors and precise GC; baseline 8,718/8,718. Qualification: `vibe/impl/Lambda_Impl_Place_Borrow_Views (done).md`. |
| D5.4.5 | Decided 2026-09-14 (CW35, `vibe/Lambda_Design_Runtime_COW.md` §11.12): move-out binds (`var left = node.left; …; node.left = branch; left.right = node`) borrow their place; static rule in `build_ast` (`rmw_moves_out`), runtime spine test shared with CW34. Fixture `test/lambda/proc/cow_move_out_bind.ls`. JetStream splay does not benefit yet: its `splay_node` binds store back inside `if` branches, so the rotations receive already-shared roots and keep the snapshot bind. |
| D5.4.4 | Ruled 2026-09-17 (user). **Implemented (partial scope) 2026-09-17 (Tune29 T29-1, `vibe/impl/Lambda_Impl_Tune29.md` §12):** synthesized handles for record places spelled repeatedly on declared `var` roots, planned once in `build_ast` and ignored by T0; writing binds un-share through `cow_place_leaf_fixed`. Deltablue2 hot functions −35% instructions, −5% time; roots with a CW34 named borrow and `for` statements are excluded. Originally recorded as not implemented (CW37, `vibe/Lambda_Design_Runtime_COW.md` §11.14 + Appendix D). Motivation: the typed lane re-navigates `w.cons[cid]` on every access — ~38 MIR instructions per field read and ~42 per field write against one in the c2m port, the whole deltablue/havlak/richards/splay/cd family (`vibe/impl/Lambda_Impl_Tune29.md`; evidence `temp/r46/`). The star stays: named borrows, `for` loops and non-record places are still outside the implemented scope. **2026-09-17 (Tune29 §20):** a named handle may also be passed as a plain argument to a procedure that keeps no root; MIR joins its may-be-shared binding facts at control-flow merges (LR12-17). |
| D4.2.5 | Corollary ruled 2026-09-17 (user). **Partially implemented 2026-09-17 (T29-2 §11.5):** typed path stores compare no shape pointer at any step below a declared root. The certificate guard is still re-run per access (deltablue2 `c_choose_method`), because the carrier `w.cons` may change between accesses; removing it needs D5.4.4's handles (T29-1). **2026-09-17 (Tune29 §20.2):** a `T[]` declaration initialized from an admitted record's field admits any container inline and checks only null. |
| D4.2.6 | Ruled 2026-09-17 (user). **Partially implemented 2026-09-17 (Tune29 T29-2, `vibe/impl/Lambda_Impl_Tune29.md` §11.3–§11.5):** required container fields of a trusted record are read with no empty-slot arm, a member read through such a field needs no receiver null test, and typed path stores test only contracts that admit null. The audit of Lambda-side writers found and fixed one hole: the JIT accepted `null` in a required field of a contract-constructed literal (the field proof mistook a declaration wrapper for an optional). Residue: `bool[]` has no native lane (admission still walks elements, T29-5); host-built values are unaudited by design. |
| D5.4.4 | Revised 2026-09-16 (v3, user ruling): the borrow is admitted wherever it is unobservable under S9.1.2; path shape, handle writes, store-back placement, nested handles and handle rebinds are implementation details (CW36, `vibe/Lambda_Design_Runtime_COW.md` §11.13), fixed only by the tier-shared static decision, the runtime spine test, and store-back-or-mark on every writing path. Motivation: JetStream splay `splay_node` stores back inside `if` branches — 826k map copies and 32% GC per run under v2. **Implemented 2026-09-16 (T28-8, `vibe/impl/Lambda_Impl_Tune28.md` §9.16):** `rmw_branch_local` in `build_ast.cpp` tracks per path whether the handle may still hold the unmarked leaf and whether it wrote it unstored; a store-back is final (skips capture) only when nothing after it names the handle; loops naming the handle and plain handle rebinds refuse. The decision now runs in `lambda_ast_finalize_script`, after every procedure signature exists, so a `var` pass to a procedure defined later counts as a write. splay2 map copies 826k→683k; the residue is rotation stores of the caller's `var` root (a call-site move convention, not a store-back shape). |
| D5.4.4 | Revised 2026-09-14 (v2, Tune27 §10.12): sibling-member access to the root and a return that precedes every use of the handle are admitted inside the region; cd's table put (`var keys = t.keys; var vals = t.vals; …; t.keys = keys; t.vals = vals`) now borrows both handles — 2,000 → 2 array copies per 1,000 puts. Fixture `test/lambda/proc/cow_rmw_sibling_borrow.ls`. Decided 2026-09-06 (CW34, `vibe/Lambda_Design_Runtime_COW.md` §11.11): read-modify-write handle borrows — tier-shared static shape in `build_ast`, runtime spine test `cow_bind_rmw_handle`; havlak's array copies 205k → 41k per run. Fixtures `test/lambda/proc/cow_rmw_borrow.ls`, `test/mir/lambda/cw34_rmw_borrow`. |
| D5.6 | Name identity is a PROPOSAL (rev 5): W1/W2 integer schemes can start now; W4 stage 3 blocked on the MIR-cache reconciliation (NI §8). |
| D5.7 | Const pool / MarkPack is a DRAFT (rev 4): baked-pointer census verified against emitters 2026-07-31; phases CP-P0..P4 not started. |
| D6.1.1 | **Implemented 2026-10-05** (Runtime_Async P0–P5): Lambda tasks and JS generator, async and module bodies of both tiers run on activations; the `LambdaAsyncFrame`, the JS MIR state machines and the interpreter replay are deleted. Parked root segments are traced through their carriers; beyond a warm window cold parks are compacted (RA13). Gates: Lambda baseline 6206/6206; test262 MIR with the untouched base's exact failure set, AST 40261/40261. Open: JSCU25 weak registration of JS async activations. ([Runtime_Async §10.1](../Lambda_Design_Runtime_Async.md)) |
| D6.2.2 | Generated-function caller homes are deleted. The retained `MirScalarHomeBinding` coloring is payload ownership for C/helper and hosted-language boundaries, not an alternate function-return ABI. Per-source mutable-binding storage and loop back-edge reclaim remain unimplemented (DO24); they are independent of companion-lane transport. |
| D6.2.3 | Lambda helper-adopt emission is retired as of 2026-08-17: its emitter sets the v3 no-rehome rule and fresh Lambda MIR has zero `lambda_item_adopt_scalar_home` calls. LambdaJS keeps the conservative helper rehome path because helpers may return module-state scalar Items; the attempted global skip changed `regression_side_stack_frame_gc` and was reverted. Shared machinery also remains for foreign hosted compilers and explicit C/host ownership boundaries. Loop back-edge reclaim and the associated peak-side-stack proof remain open under `DO24`; revisit JS only with a per-helper ownership proof. |
| D6.2.1 | **2026-11-03: unified outcome metadata and normal admission through error unions are qualified (S7.4.2v4/S11.4.2v3/S11.4.6v4): Lambda 6,456/6,456, focused tier/GC 288/288, MIR emission 282/282 and MIR GC 352/352. The following companion-lane qualification predates unification.** LambdaJS P2.5 and P5a landed 2026-08-17: Lambda and LambdaJS compile only the companion-lane ABI; `LAMBDA_RETURN_V3`, `FN_COMPANION_HOME`, generated `_scalar_home` parameters, and direct-call donation are deleted. Record of the retired v2 design: every function carried a hidden trailing home address and copied a wide payload into it. Retained side-number-stack slots belong only to destination ownership, C helpers, or hosted-language APIs under D6.2.2. **Qualification scope:** production T0/MIR Direct; experimental Lambda MIR MVP adaptation is deferred ([LR01-22](../Lambda_Issue_Ledger.md#lr01-22)). |
| D7.1.2, D7.2.3 | **Parent-fn extension qualified 2026-11-02: Lambda/input baseline 8520/8520, including MIR GC stress 350/350.** Shared indexed flow facts select capture permissions before T0/MIR lowering, with fixed-point known-call return/retention summaries. A parent local and its confined sibling pns retain the same internal one-element GC Array home using existing owned Item/scalar slots; reads preserve scalar ownership, and rebind/COW replacement publishes through that home. Precise frame/environment roots preserve lifetime. Pure and escaping captures snapshot at creation; named direct calls reuse the declaration environment. AUTO refuses shared-home satellites under the existing fallback contract; eager MIR supports them. No fn-to-pn call exception or native-stack scanning. Focused tiers, native shared-home/GC tests and optimizer checks pass. All 15 local-state fixtures match interpreter/JIT with zero fallback; full-sweep and checker failures are base-controlled or explained in the implementation record. Legacy pn-parent capture debt remains separate. [Implementation record](<Lambda_Impl_Function_Local_State (done).md>). **Follow-up:** ordinary and decomposed bindings share home initialization; source values stay rooted across projection and home allocation, and decomposition participates in the indexed escape graph. Final follow-up Lambda/input baseline: **8533/8533**, including 353 GC stress tests; see [follow-up qualification](<Lambda_Impl_Function_Local_Decomposition (done).md>). |
| D7.1.3 | `may_await` (v2) feeds call metadata and GC planning as a may-park effect, and still gates the MIR region producer/consumer pass. **`may_defect` built 2026-09-27, fail-closed.** build_ast computes a conservative fact per function (`TypeFunc::defect_known`/`may_defect`); it fixes literal slot layouts and the S11.4.3 flag for both tiers and serves imports. The JIT solves the least fixed point per compile (`mir_solve_may_defect`), each origination site judged by the emitter's own gate, and treats anything outside its table as defect-capable. build_ast's fact over-approximates the solve, and the JIT narrows the S11.4.3 flag with its own answer except for an operand whose type admits error. `may_return_error` still carries both meanings in the variant effects. ([LR12-24](../Lambda_Issue_Ledger.md#lr12-24)) |
| D7.5.1 | **JSCU25 implemented 2026-10-05, in the lean form** (Runtime_Async §10.1): a parked JS async activation registers weakly with the attached scheduler (`lambda_scheduler_weak_enter/leave`, a token naming the scheduler's serial). It counts in `lambda_scheduler_live_count` until it settles or its carrier is collected, so a Lambda run's end-of-run drain lets an unawaited JS async call finish. Readiness stays with the microtask queue, and the run queue never resumes a JS activation. The record is the GC-owned async carrier, and its Promise is the handle; there is no `LambdaTask` per JS call and no mailbox. A drain with only weak registrations left waits while anything besides its own watchdog keeps the loop alive; when idle it gives queued promise jobs a checkpoint, collects, and then ends without error. Fixture `test/lambda/conc/js_async_weak.ls`; gtest `WeakRegistrationsCountUntilTheyLeave`. |
| D7.5.2 | Worker tier pending entirely: process isolation first, thread isolation gated on the isolate-state audit and DO20. |
| D9.1.3 | Static modules implemented (rev 29, P0–P6) except Class F: the rt→radiant boundary is a ratcheted 165-import baseline; P1c constructor consolidation deferred. |
| D9.2.3 | **Implemented 2026-10-06.** `make lambda-wasm` builds the synchronous browser REPL from an explicit source/dependency manifest, with no MIR, allocation tracker, native string-SIMD unit or excluded acquisition/ambient providers. CSS/graph/math input and CSS/graph/LaTeX/math formatting, image-processing kernels and public history/event builtins are excluded. HTML parsing and template apply are retained, with REPL registration/rollback/reset verified. Node and Chromium each pass 283 embedding checks; native REPL tests pass 41/41 and the full native Lambda baseline passes 6,243/6,243. Optimized WASM + loader: 1,891,405 bytes raw / 729,874 bytes gzipped. Interpreter corpus and broader browser coverage remain verification work; the C API is initial, and external imports are rejected. Working record: `vibe/Lambda_Design_Static_Modules.md` §13.4; reproduction and ABI: `doc/dev/Lambda_WASM_Build.md`. |
| D9.3.4 | **v4 clause implemented 2026-10-22** with its first module, `lambda.io.mail`: `builtin_module_name` maps the full path, and `resolve_builtin_module_member` the qualified spelling `lambda.io.mail.<fn>`, to the `io_mail_*` rows, which park like `io.fetch`; `io.mail` is not a spelling, and an unaliased import is refused because its `open` and `send` would shadow the built-in ones. Regressions: `make test-mail`, `test_mail_imap_gtest` (`vibe/Lambda_Demo_Mail.md` M0 checkpoint). v3 record (implemented 2026-10-15): Shipped imports mirror their namespace below `<lambda-home>/package/`; math, PDF and LaTeX source and resources live in `lmd/package/doc/{math,pdf,latex}/`, with no resolver exceptions or old namespace aliases. Native document bridges, PDF export and TeX resource loading use the matching paths. The checkout and release share `./lmd/`, copied unchanged by `utils/prepare_release.sh`. Built-in `lambda.math`/`lambda.io` aliases and `lambda.sys.*` remain separate from script packages. Regressions: `test/lambda/lambda_namespace.ls`, the reserved-root negative fixture, and `test/lambda/{math,pdf,latex,eps}/`. |
| D10.2.1 | Implemented 2026-09-07. The shipped `lmd/package/dom` behavior package owns the shared descriptor/context/plan/result pipeline, text and structural editing, formatting, objects, clipboard, history, `designMode`, `execCommand`, and all five `queryCommand*` surfaces. Native Radiant retains only platform transport and generic, checked DOM/Selection/Range/clipboard transaction mechanisms. Applicable WPT and pinned Chromium contenteditable manifests, package-disabled behavior, editor integration, form regressions, Lambda/Radiant baselines, and lint pass; the release/lifecycle record is `vibe/radiant/Radiant_Editable_UA6_Report.md`. The separate `Radiant_Design_Edit_History.md` expansion (including form-history migration and its different retention contract) remains a proposal and does not alter this ruling. |
| D9.3.5 | **Implemented 2026-09-29.** `lambda_resolve_import_module_path` is the one resolver (AST import, dependency prebuild, document-transform package loader); it returns NULL for a bare root other than `lambda`, and `resolve_import` reports E216 after the built-in and registered-module checks. `run_source_fixture` in `test/test_lambda_opt_gtest.cpp` runs checked-in scripts in place for the same reason the ruling states. |
| D10.2.2 | **Partially implemented 2026-10-12; bindings 2026-10-13.** Expansion is the only front end: `parse_latex_direct` always runs the engine (the unexpanded branch and the `LAMBDA_TEX_EXPAND` knob are gone), the native document transform and the doc viewer parse through `lambda.doc.latex`, and the script's macro substitution is retired. Engine, bundled programming packages, expl3 through an in-memory format, the path gate and the resolution order are built; 151 trace records match `pdftex -ini -etex`. Lambda-owned bindings run from the bundled `binding/` directory: `physics.sty` and the `mathtools.sty` macro layer. Residue: the layout-metrics hook (box widths are estimates), in-memory write streams for multi-pass runs, the `local-package-shadowed` diagnostic, engine reads through the central IO door, and an on-disk format cache. Pointer: `vibe/latex/Lambda_Design_TeX_Engine.md` §10. |
| D10.2.3 | **Partially implemented 2026-10-14.** Font-independent export, strict XML input and runtime/CLI conversion implement the first delivery stage. The next stage adds a separate MathML box builder, generated operator dictionary, shared SVG geometry, and Radiant package-layout integration that retains the source DOM and inert annotations. The supported subset uses selected MATH/Core constants, never TeX companions; full Core/CSS and accessibility integration remain open. Scope and verification: `vibe/Lambda_Pkg_Math.md` §11. |
| D9.4.6 | **Implemented 2026-10-07 on macOS; Linux generated but unverified; Windows not covered.** `utils/generate_premake.py` turns the one list into `-Wl,-exported_symbols_list` (macOS) or `-Wl,--dynamic-list` (Linux) for every Jube-capable host configuration, debug included, and release compiles with `-fvisibility=hidden` again (dropped 2026-07-20 for hosted Python, with `-export_dynamic`). The initial list is the 400 symbols the built `lang-python` and `node-*` modules import: 102 from vendored mbedTLS, mpdecimal and RE2 archives, 298 Lambda-owned. Release exe size, before → after (macOS arm64, stripped ThinLTO release builds of commit `f6e11aa33` that differ only in the visibility and export flags): `lambda.exe` 21,702,952 → 19,158,984 bytes (−2,543,968 bytes, −11.7%); exported symbols 15,971 → 400. Removing `lang-python` (2026-10-07) dropped its 126 entries: the list is now 274 symbols (79 vendored mbedTLS, 195 Lambda-owned) imported by `node-core`, `node-crypto`, `node-fs` and `node-net`, and the release host exports exactly those. `utils/check_host_exports.py` runs after every release link (host ⊇ list: the linker silently skips a listed hidden symbol), and `make check-host-exports` also checks module imports ⊆ list. Windows hosts still link `--export-all-symbols` for the module import library. Working record: `vibe/Lambda_Design_Jube_Architecture.md` JA5.1. |
| D9.5.1 | Native-module POC 1 remains unstarted; the engine-owned Promise VMap is designed by JR7/Tune7 but not yet implemented. |
| D9.5.3 | **Not implemented since 2026-10-07 (USER):** `lang-python` and the host's hosted-language and hosted-compiler services (source, diagnostics, output, session memory, guest execution with the MIR compiler cursor, module graph, runtime catalog, data API, language descriptors and `lambda <language>` dispatch) were removed with a Jube ABI bump (`JUBE_ABI_VERSION` 8, `JUBE_HOST_API_VERSION` 5). No guest language is hosted; the ruling constrains any future one. Record: `vibe/Lambda_Design_Jube_Lang_Hosting.md`. |
| D9.5.4 | Implemented in DOM4 (2026-08-14): `host_ops`, `legacy_ops`, `JubeHostObjectOps`, and the vmap `string_key_item` re-materialization shim were removed; record-owned hooks are the only host-object protocol. The protocol remains unchanged; the D9.5.5 TypeId-order revision advances the current Jube ABI to version 6. |
| D9.5.5 | Partially implemented 2026-09-08. Distinct VArray/Velmt TypeIds and GC-traced headers, semantic type/validation/index/iteration/equality/order/printing dispatch, Jube ABI 6 carrier selection, carrier-neutral host metadata, and exact shaped-field storage are landed. The reordered physical tags are `RANGE=16`, `ARRAY_NUM=17`, `ARRAY=18`, `VARRAY=19`, `MAP=20`, `VMAP=21`, `ELEMENT=22`, `VELMT=23`, `TYPE=24`, `FUNC=25`, `ANY=26`, and `ERROR=27`; Error is the last valid TypeId. Native Radiant snapshots are renamed `RadiantVelmt`; custom-layout handles and all shared DOM Node-family wrappers use Velmt. Live child/element/form/lookup/options/selected-options/attribute/token/CSSOM collections and static query/radio/rect/mutation lists use direct or retained VArray backends; Lambda child-list access reads the native backend directly under the fixed-DOM run contract and has no `snapshot_for_lambda` hook. Remaining gates are FileList/DataTransferItemList/SVGTransformList migration, the VMap status-vtable follow-up, synthetic splice/materialization coverage, and the full GC/WPT/performance matrix. ZIP input adds optional fallible preparation before cached counts in virtual ABI 2 (2026-10-08), preserving deferred payload errors under S14.3.1v2. Jube ABI 9 rejects modules compiled against the earlier callback layout. Working record: `vibe/Lambda_Design_Type_Virtual.md`. |
| D9.1.2 | Partially implemented 2026-09-14: `InputManager` owns a process-persistent `InputScriptCache` with named cache contexts, exact-byte source identity, scopes/leases, opaque AST/MIR slots, policy controls, accounting, request-scoped source-generation retirement, and Lambda plus JS/Radiant source or MIR adapter entry paths. Lambda source records and admitted synchronous static-JS-import module records retain direct-import edges by persistent unit ID; a changed file-backed dependency retires its full importer cone before a cached artifact is reused, while leases defer destruction. The common cache also owns AST/MIR-key single-flight states; Lambda uses them, and a minimal per-key poison state blocks reuse until source invalidation. Full executable-source inventory, JS/guest adapter adoption, remaining JS/guest dependency cones, richer failure recovery, and eviction remain open. Working record: `vibe/Lambda_Design_Script_Cache.md`. |
| D11.1.1 | T1 verification layers staged; T2/T3 directional, neither built (not required until a third-party module story). |
| D11.1.2 | Host file/source byte and identity clients, ordinary input/directory, TeX/Structurizr resources, runtime path metadata/traversal, task-read clients and bounded private IPC are extracted; the remaining IO surface is incomplete (`js_fs`/`js_os`/`js_net` raw-IO violations remain in the burn-down list); `dynamic_lookup` laxity is acknowledged debt. |
| D11.2.1–D11.2.4 | **Ruled 2026-11-02 (USER); components only.** Neutral policy, anchored files, host grant store/file broker, target-bound approval challenges, bound-client `output()`, Lambda source/import/cache/prebuild, ordinary input/directory/parser resources, path metadata/traversal and task reads (interpreter and MIR JIT), bounded broker IPC with captured-authority publication leases and expiry checks, an experimental signed T0 Lambda worker and a basic macOS syscall-denial probe are implemented. Scoped snapshots, document heads and write-set transactions have context-owned precise roots; snapshot authority epochs invalidate captures, and task reads recheck delivery and join cancellation. Unsupported legacy mutation/search, command, fetch, mail/secret and loopback adapters refuse bound callers before native work. A host ad-hoc admission core now builds source manifests and a base protected catalog (including absent names), binds profile identities and restores supported file grants; signed tests cover host-process restart, revocation and changed-code refusal. A dedicated macOS supervisor now validates the publisher/sealed worker bundle and suspended child identity, launches with a clean environment/handle allowlist, owns once-only terminal consent and monitors CPU/physical-footprint/wall/output budgets plus kernel bootstrap caps. Broker-owned per-principal data/cache and per-admission temporary files are bound to the admitted channel through virtual mounts, a protected physical namespace, owner-object identity checks and bounded class-wide byte/entry accounting; no private mount grants source authority. Host-only cache eviction and joined session finish use strict anchored removal, with observable cleanup failure and nonblocking lease-based abandoned-temp recovery after host death. The experimental macOS launcher clears registered user Mach rights, task exception ports and bootstrap access before startup; its worker rejects ptrace through a syscall filter. Before runtime/source acquisition, the worker validates one bootstrap thread, the actual null input device, anonymous output pipes, a connected local broker socket and absence of extra descriptors; native controls show that pre-held file reads and writable mappings otherwise survive policy installation. Native controls demonstrate that an inherited memory entry otherwise permits parent-page modification after sandbox installation. TASK_ACCESS_PORT and full debugger qualification remain open. A constructor gate also leaves the direct-linked worker unqualified: closed-fd mappings survive entry. An opt-in libSystem-only bootstrap now applies policy before loading the separate T0 runtime, with one literal runtime-image read/map allowance and nested-code verification. Local composition controls are separate from successful hardened publisher loading; the user skipped signing-identity setup, so that deployment gate remains open. A deny-native-IO macOS process-policy prototype is under qualification; the user approved retaining its private Darwin backend experimentally on 2026-11-03, with qualification required before any default rollout. Protected updater control/catalog state, scoped pause/disable/restore and offline precedence, persisted job reservations/backoff and host console management are implemented; no app fetch capability was added. Trusted storage inspection and explicit, random-intent-bound persistent-data reset are implemented with live-session refusal, bounded cleanup and host-death resumption controls. Anchored creation now syncs child/parent namespace edges and new lock/lease files; protected state/private mounts require macOS full-sync barriers, with fault-tested refusal and uncertain-commit handling. Protected per-principal quotas and trusted CLI edits/default restoration now persist generations, refuse stale/live edits and retain existing data across reductions. Default production integration, installed publisher qualification, approved OS backend and complete per-principal isolation/crash qualification, corrupt-data/full power-loss recovery, installed-app uninstall integration, graphical quota and persistent permission UI, complete installed-app/global catalog coordination, remaining IO/non-Lambda loading adapters, complete quotas and end-to-end updater verification/staging/activation remain pending. A host-only anonymous HTTPS pull component now checks every redirect origin and resolved peer, bounds responses and cancels on policy changes; successful bytes remain unverified and update delivery remains unavailable. Lambda Script verifier components now implement strict signing-byte parsing, root thresholds/rotation, freshness/rollback, delegated targets and checkpoint sequencing, using native public-key signature primitives and a protected opaque per-package cache. A dedicated bounded host bridge now runs the immutable embedded Lambda verifier in an offline early-confined interpreter process, applies live-job-bound fetch/checkpoint actions and returns reservation-bound opaque artifacts. Owned TLS composition, persisted-cache retry and cancellation controls pass; independent pinned conformance currently passes 112/119 with all failures retained, and 688 strict-JSON differential inputs pass on interpreter/JIT. Cache reuse reauthenticates each current delegation edge and unchanged polling checks cached expiry. Opaque UTF-8 role labels stay inside the unchanged cache layout; canonical URI encoding and an independent native gate preserve URL/file-domain separation, with 728 differential cases on native/interpreter/JIT. A bounded inactive ZIP decoder now validates the complete image namespace in Lambda Script and streams exact member bytes from an early-confined offline interpreter, with fixed-size per-entry acknowledgements and no file-write or package-code execution authority. Its initial member-name profile is printable ASCII with collision/device-name refusal; 53 owned archive/IPC controls include maximum size/count boundaries and cancellation. A native host adapter now consumes only live reservation-bound opaque artifacts, independently gates decoder paths/types/counts and writes private inactive images through strict anchored full-sync operations. One pending image per profile, random identity, exclusive claims and checksummed intent/receipts bound storage and bounded offline/paused cleanup; ready publication follows clean decoder exit and a fresh policy lease. Native mechanism and owned signed HTTPS/verifier/decoder composition checks pass, including tamper/traversal refusal and retained-data canaries. Successful hardened publisher launch, full conformance, managed-image publisher/capability admission and active publication, activation/recovery and installed-catalog coordination remain unqualified. Delivery stays disabled. See `vibe/impl/Lambda_Impl_App_Safety.md` §6; no end-to-end sandbox is claimed. |
| D11.2.5 | **Ruled 2026-11-03 (USER); partial.** The experimental trusted host retains data unless explicitly reset, with bounded inspection and interrupted-reset recovery. Installed-app uninstall/retention integration and lifecycle UI remain pending. See `vibe/Lambda_Design_App_Safety.md` §7.1 and `vibe/impl/Lambda_Impl_App_Safety.md` §6. |
| D11.3.2 | Partial: explicit procedural capture, separate OS/document grants, bounded copied PCM, stop/revocation/loss/teardown and gated monitoring through dry/named bus routes are implemented for the macOS core profile. Physical permission/hotplug qualification and other OS backends remain open; D11.3.1 output permission never grants input. |
| D11.3.1 | Ruled 2026-10-19 (USER); partly implemented 2026-10-19 for Lambda rows. `radiant/audio.cpp` judges each document by its own URL (`file:` or none is local), keeps a sticky activation flag set by trusted presses and non-modifier keys in `handle_event` and by EventSim pointer/touch input, and honours the host grant (`view --autoplay`); `dom.speak`, `dom.midi_command(…, "play")` and unmuted `dom.audio_play` check it (verified by `RadiantFrameDelivery.AudibleOutputGateFollowsDocumentOrigin`). Residue: `<video autoplay>` still starts unmuted for every origin (`radiant/resolve_htm_style.cpp`); JS media and Web Speech (`NotAllowedError`) wait for LambdaJS speech (Radiant_Design_Speech M5); a `<base href>` replaces `DomDocument::url`, so a local page with a network base is judged as network. |
| D8.3.3 | Revised 2026-09-14 (v10): every publishable `var` position -- untyped, typed container, typed scalar or record, optional or not -- consumes its CW33 home in the generated prologue and publishes its final boxed value in the epilogue (encoded through the declared contract lane, so a null `float?`/`bool?`/`string?` publishes `null`); the `_b` adapter forwards the cell it consumed to the raw body and stores the reloaded binding back; a typed scalar/record argument whose binding is a lane local is homed from its boxed argument slot and decoded back into its lane after the call. The v8 fail-closed pin on bodies that rebind a typed `var` parameter (and their direct satellite callers) is therefore lifted: such a body promotes like any other and the rebind reaches the T0 caller through the same home (fixtures `test/lambda/proc/cow_var_typed_rebind.ls`, `interp_typed_var_rebind.ls`, three tiers byte-identical). Revised 2026-09-07 (v9): a satellite image co-compiles the target's direct-callee cluster in module order (direct native edges among members, module-wide call-site inference, every member's boxed entry published; compiled/pinned/unsupported callees stay dynamic) -- diviter 20 s → 349 ms, richards 786 → 620 ms, deltablue 464 → 348 ms auto e2e, each at eager parity; DO30 records splay. Revised 2026-09-07 (v8): typed `var` parameters are admitted to satellites -- every `var` position travels through the CW33 home cells on the boxed edges, the `_b` wrapper prepares, admits and stores the container back through the home, and a satellite's dynamic edge transports and reloads typed positions keeping their raw descriptor; a body that rebinds a typed `var` parameter, and every satellite that would call it directly, stays in T0 (navier_stokes, nbody2, json2 promote; DO29 records the eager tier's own rebind loss). Revised 2026-09-07 (v7): a loop-bodied procedure promotes at its first entry (once-called `main` loops: mandelbrot 5.5 s → 0.8 s, matmul 3.2 s → 56 ms auto e2e on the debug build); aggregate/structured value-parameter contracts are admitted to satellites (nbody2, pnpoly2, deriv2, ray2, array1); a satellite's read of an annotated `string[]` module binding takes the generic index path because the T0-initialized slab carries the boundary's native carrier (DO28). Revised 2026-09-06: satellite admission widened to plain `any` parameters, bodies with local `var`/rebinding statements and indexed/member stores, and untyped `var` parameters (write-back through the CW33 home transport on both tier edges, borrowed-call dispatch mode; aggregate contracts and typed `var` parameters stay pinned); satellites rebind the module's static const image after interning, and dynamic-call arguments append verbatim. Decided 2026-08-25. Normal Lambda files/modules use the first-party C hybrid recursive-descent + Pratt parser, which reduces directly to the shared typed AST and retains no Lambda CST. `LAMBDA_PARSER=tree`/`tree-sitter` remains an explicit reference/rollback path; `compare` checks Tree-sitter syntax acceptance while publishing the direct AST. The REPL and legacy inspection paths remain reference-parser consumers until their fragment/source-span migration is complete. The default script selector chooses `AUTO`; `interp` forces T0 and `jit` retains the eager whole-module path. P2 satellite promotion uses a default function-entry threshold of 5. A direct validated self-tail edge uses the same tail-edge threshold and may hand the active activation to an already-published boxed satellite entry; arbitrary-PC / loop OSR is not implemented. P2 fails closed for nested definitions, indirect Lambda calls, object-field identifiers, match expressions, and invalid batch-retained module state; scalar module reads and dynamic multi-argument calls use the shared boxed ABI. Status and gates: `vibe/Lambda_Grammar_Parser.md` §7 and `vibe/impl/Lambda_Impl_Ast_Interp.md` §3.1. |
| D8.3.5 | Revised 2026-09-22 (v11): static file-backed import prebuild uses one bounded closure-wide task pool. Parse-accurate discovery immediately canonicalizes, deduplicates, and queues each static-import request; a completed child notifies and releases all direct parents, so a shared child is built once without dependency-depth batch barriers. Worker tasks publish only AST templates and never wait on child work; the cache retains its ordinary single-flight handling for separate closures. Initialization, execution, and MIR stay on the receiving runtime. Working record: `vibe/Lambda_Design_Script_Cache.md` §12.1. |
| D8.3.4 | Revised 2026-09-22: AUTO P2 promotion queues one deduplicated, snapshot-safe target definition to the bounded process-wide compiler service and continues the triggering call in T0. A worker produces only a private, immutable MIR image from frozen facts; it never mutates a live Script context, AST analysis fact, module image, or Function entry. A later ordinary safe point generation-checks and publishes the target boxed entry. Stale/failed/unsupported work pins that execution-local definition to T0. This adds no OSR or active-frame replacement; private direct-callee cluster images remain later work. Working record: `vibe/Lambda_Design_Script_Cache.md` §12.1. **Conformance fix 2026-09-22 (LR01-16):** the worker placed its image's property-key suffix from the owner slab's key count. That thread-local read saw an empty slab, and publication kept any suffix whose count differed, so every image after the first resolved member names through the first image's keys. The suffix is now placed when the safe point binds the image, linked by key-ID content and recorded in `LambdaModuleLayout::property_key_base`, which the generated code reads; `LAMBDA_SATELLITE_SYNC=1` pins publication for tests. |
| D8.3.5 | Revised 2026-09-22: static import prebuild moves from a per-root closure graph to a process-global canonical profile/path task-future registry and one bounded pool. Root discovery is fire-and-forget; tasks immediately request all children, deduplicate a shared child, and release only direct parents by continuation. AST build workers never wait on child work. Ordinary Lambda/JS import consumers wait only for their direct dependency future; prebuild-worker loaders bypass that wait. The cache separately validates exact source identity before an artifact is consumed, and cycle/prebuild failure falls back to ordinary-loader diagnostics. Working record: `vibe/Lambda_Design_Script_Cache.md` §12.1. |
| D8.3.2 | Ruled 2026-10-02 (USER); **partially implemented** 2026-10-02 (branch `worktree-interp-tune2`). Implemented: the per-loop back-edge trigger at 10000, retirement of the loop-owner first-entry trigger, and handoff at the loop head. The loop entry is a synthesized procedure over the live-in locals (the body's statements from the loop onward), entered through its boxed wrapper, so every live-in is admitted like an unknown caller's argument. Residue: eligible loops are only `while` statements directly in a `pn` body, and back-edges of their nested loops count toward them; the loop-triggered image does not carry the definition's own boxed entry. Under AUTO, `mandelbrot2` 12.0 s → 0.10 s and `matmul2` 6.3 s → 0.11 s (eager JIT 0.06 s / 0.05 s, debug build). AUTO corpus differential 1092/1092 identical; with `LAMBDA_LOOP_JIT_THRESHOLD=1 LAMBDA_SATELLITE_SYNC=1` every eligible loop hands off and the corpus matches `interp` except one pre-existing sync-mode satellite abort. Plan and evidence: `vibe/impl/Lambda_Impl_Interp_Tune2.md` §12.2. Working record: `vibe/Lambda_Design_Ast_Interpreter.md` §5.1.1. Renamed 2026-10-03 (USER), meaning unchanged: the tier selector `LAMBDA_TIER` is `LAMBDA_EXEC_BACKEND`, `LAMBDA_JIT_THRESHOLD` is `LAMBDA_FUNC_JIT_THRESHOLD`, `LAMBDA_JIT_BACKEDGE` is `LAMBDA_LOOP_JIT_THRESHOLD`, and LambdaJS's `JS_EXECUTION_BACKEND` is `JS_EXEC_BACKEND`; the old names are no longer read. |
| D8.3.1 | **Implemented 2026-10-03.** Explicit interp rejects unsupported scripts/imports before execution, including task-backed MIR satellites and cached AST activation. AUTO retains fallback; explicit jit retains eager MIR. Lambda baseline: 6,180/6,180; strict-pin regression cases: 6/6. Evidence and existing differential failures: `vibe/impl/Lambda_Impl_Typed_Performance_Audit.md` §6–§7. |
| D8.3.2 | **Implemented 2026-10-05** (Runtime_Async P1, P4): T0 evaluates `start`, `select` and scoped tasks, and the strict-`interp` rejection of task-backed bodies is gone; JS promotion admits async and generator definitions and loop handoff refuses only a loop whose tail can suspend. Forced-promotion sweep over every `test/js` golden identical to the base. |
| D8.3.6 | **Implemented 2026-10-06** (branch `worktree-repl-interp`). `run_repl` owns one `InterpReplSession`; whole-history `run_script_mir` replay and the init-failure fallback are deleted, and a `jit` selection becomes `auto` with a first-call promotion threshold. The entry transaction (`ReplEntryTxn`, `runner.cpp`) returns OK, INCOMPLETE (the C parser's status, no pre-check), REJECTED or FAILED and prints each diagnostic once; an unsupported construct or import is rejected by name. Imported cones initialize before the entry runs. SIGINT is armed per entry; T0 polls it at call entry and back-edges (`LAMBDA_FAULT_INTERRUPTED`), and promoted satellite code is not polled. No Tree-sitter symbol is reachable from the REPL path. Session services: `.env`, `.type`, `.time`, `.load`, `.save`, Tab completion and a history file. Gates: `LambdaReplSessionTests` and `LambdaReplTests`. [Repl_Interp §3, §6](../Lambda_Design_Repl_Interp.md) |
| D8.3.2 | **Implemented 2026-10-12** (USER ruling the same day). The frame plan numbers a plain `for` statement of a `pn` body as a handoff loop; at its head test T0 passes the walk's snapshot and next position after the live-ins, and the synthesized continuation's first statement is a copy of the `for` over the snapshot parameter starting at the position parameter (`AstForNode::resume_from`). The boundary test compares the statement's entry scratch level, since a `for` walk holds its own scratch slots. Fixture `test/lambda/proc/for_loop_handoff.ls` (12 shapes: `of`/`at`/`in`, pairs, `where`, `break`, `continue`, early `return`, a `var` parameter, a rebound source) hands off at forced thresholds 2 and 5 and matches T0. A one-call 490k-character walk ran 98 ms as `for` against 58 ms as `while` on `auto` before this. Record: [Ast_Interpreter AI25](../Lambda_Design_Ast_Interpreter.md). **Parent-fn follow-up (2026-11-02):** direct function while suffixes retain callable colour and functional completion in their continuation. A collected prefix remains T0 unless an unconditional terminal return/raise discards it; shared capture homes remain T0. Forced-threshold/GC fixtures cover COW live-ins, early returns and multiple results. Record: [function-local benchmark qualification](<Lambda_Impl_Function_Local_Benchmarks (done).md>). |
| D8.1.1 | Revised 2026-09-14 (v3, USER): the obsolete `grammar-lambda.js` and complete-oracle claim are retired. `lambda/tree-sitter-lambda/grammar.js` is the best structural reference and first-cut fuzz verifier, but known corner cases require reviewed fixtures. The first-party C parser remains the final production implementation, not an unquestioned oracle; every C/Tree-sitter acceptance disagreement is adjudicated against the formal syntax rulings and current vibe records, then pinned by a fixture and fixed on the incorrect side. Generated `parser.c` remains reference-only and is never hand-edited. Working record: `vibe/Lambda_Test_Fuzzy.md` §3 and `vibe/Lambda_Grammar_Parser.md` CGP5v2. |
| D8.4.2 | Revised 2026-09-23: the unset `JS_EXEC_BACKEND` now selects AUTO. Interpreter-supported units execute through the retained AST path and may promote admitted hot definitions to P2; unsupported units retain whole-module MIR. `ast` remains explicit and fail-closed, `mir` remains an explicit whole-module selector. MIR link-interface and O1 decisions are selected through shared `mir_select_link_interface`, so large modules/documents can install the MIR interpreter without changing generated MIR. The v18 parser, AST-interpreter, module-registry, and P2 admission record remains otherwise unchanged. Working record: `vibe/Lambda_Design_Compiling_Pipeline_JS.md` LC4.1–LC4.12. |
| D8.4.3 | Revised 2026-09-24 (USER): dynamic source — direct/indirect `eval`, the `Function`-family constructors, string timers, and `$262.evalScript` — parses into a retained `JsScript` and executes only in the AST interpreter, whatever the caller's tier or the unit selector. The MIR dynamic-code service (expression-wrapper and whole-script eval tiers, the dynamic-Function MIR cache, eval preambles, the `is_eval_direct` lowering mode) is deleted; a MIR caller keeps only the direct-eval bridge projection and write-back. Script top-level lexical realm records link their module slots on both tiers, so interpreted readers see live values and TDZ, and a projected caller binding outranks a global lexical of the same name. Dynamic source shares the AST template cache (a direct eval naming private members is parsed per call); whitespace/comment-only and lone-RegExp sources create no script. Verified: test262 baseline 40261/40261 with 0 regressions; `make test-lambda-baseline` 3754/3754 (incl. `test_js_gtest` 479/479, `test_js_script_gtest` 191/191); `test_js_gtest --baseline --full-mir` 477/477. Residue: eval-created closures lose projected caller locals after the eval returns, `eval("arguments")` in functions and `new.target` in direct eval remain unsupported (pre-existing). Working record: `vibe/Lambda_Design_JS_Interpreter.md` §5.6/JSI35; `vibe/impl/Lambda_Impl_JS_Interpreter.md`. |
| D8.4.3 | Revised 2026-09-24 (USER): a direct eval from interpreted code links its activation to the caller frame's live `JsInterpEnv` chain through one boundary record (`JsInterpEnv::eval_script`). Free names in eval code and in its closures resolve by name through that chain (`js_interp_lookup_name`); EvalDeclarationInstantiation targets the caller's nearest function variable environment (`js_interp_instantiate_linked_eval`); the eval frame borrows the caller's `this`, `new.target`, and home-object homes. A function whose body or nested arrow contains a direct eval materializes `arguments`. The interpreted side of the bridge (projection, write-back, per-activation journal frames) is deleted; MIR callers keep it, with the source scan for strict writes to bridged consts. Strict `eval`/`arguments` targets in every assignment form are parser early errors on both tiers, and the eval-source stack and function eval-origin payload that only the deleted MIR eval unit fed are removed. Verified: test262 baseline 40261/40261 with 0 regressions; `make test-lambda-baseline` 3756/3756 (incl. `test_js_gtest` 481/481); ad-hoc `test/language/eval-code` interpreter 214 → 312 of 343, MIR 217 → 218. Residue: MIR callers keep the bridge until P4 shared environment cells, so JS05-L5–L7 persist there; the interpreter keeps parameters and body vars in one function record (the `arrow-fn-body-cntns-arguments-*` eval-code cases); PerformEval's caller-dependent `new.target`/`super` early errors are not enforced. Working record: `vibe/Lambda_Design_JS_Interpreter.md` §5.6/JSI36; `vibe/impl/Lambda_Impl_JS_Interpreter.md`. |
| D8.4.5 | Ruled 2026-10-03 (USER); **not implemented** beyond the call-threshold default. JS promotion is hotness-only: `JS_FUNC_JIT_THRESHOLD` defaults to 1000 calls (was five), and a loop reaching `JS_LOOP_JIT_THRESHOLD` (10000) may hand its running AST activation to compiled code at the loop-head test, in the D8.3.2 shape (continuation over the live bindings, one-way). This supersedes the v13 sentence that no active AST frame transfers to MIR and JSI18's entry-only rule. JS keeps its own selector and knob names. Both thresholds are provisional pending release profiling. Open: P2 admission still rejects calls and outer bindings, so few loops are eligible until it widens; the satellite still re-parses its source. Working record: `vibe/Lambda_Design_JS_Interpreter.md` §9.3/JSI18v2; plan: `vibe/impl/Lambda_Impl_JS_Interp_Tune.md`. |
| D8.4.5 | Revised 2026-10-03 (USER): the `JS_FUNC_JIT_THRESHOLD` default is 100 calls (1000 in v22). A release sweep over 13 JS benchmarks found no row slower at 100 than at 1000, with most of the gap to 5 recovered (mbrot2 22.8 → 12.6 ms, paraffins 82 → 62, nqueens2 50 → 44, puzzle 51 → 42); 100 still keeps rarely called functions interpreted while each promotion re-parses its whole source (17–59 ms at 17–87 KB). `JS_LOOP_JIT_THRESHOLD` (named `JS_JIT_BACKEDGE` until this revision) stays 10000; the call knob is renamed `JS_FUNC_JIT_THRESHOLD` (was `JS_JIT_THRESHOLD`). Both remain provisional; revisit after satellites compile from the retained script (plan E2). Evidence: `vibe/impl/Lambda_Impl_JS_Interp_Tune.md` §11.5. Renamed 2026-10-03 (USER), meaning unchanged: the tier selector `LAMBDA_TIER` is `LAMBDA_EXEC_BACKEND`, `LAMBDA_JIT_THRESHOLD` is `LAMBDA_FUNC_JIT_THRESHOLD`, `LAMBDA_JIT_BACKEDGE` is `LAMBDA_LOOP_JIT_THRESHOLD`, and LambdaJS's `JS_EXECUTION_BACKEND` is `JS_EXEC_BACKEND`; the old names are no longer read. |
| D8.4.4 | Revised 2026-10-29: ordinary AST/AUTO and dynamic-source execution use MVP semantic operations and runtime-only realms; immutable activation plans and shared indexed/call kernels preserve live-cell fallbacks and precise ownership. Qualification is recorded in `vibe/impl/JS_Interpreter_MVP_Alignment.md`. The unrelated v42 extension remains partially implemented as recorded 2026-10-24. The checked method-body arrow extension is approved but its candidate is not retained: the original callback compiles and 252 native/20 forced-GC checks pass, but CSS median is 13,605 versus 13,542 ms control and overview/pricing retain the 120 s watchdog. Atlas passes with both versions. The patch and evidence remain under `temp/radiant-online-oct24-method-callbacks/`. Source and release/modules are restored to qualified D8.4.5: 249 native, 92 GC, 17 forced-GC, 121 optimization contracts, 40,261 Test262 cases and Radiant pass; Lambda retains only the known PDF mismatch. No broader gates ran for the rejected candidate. Working records: `vibe/Lambda_Design_JS_Interpreter.md` §9.4 and `vibe/impl/Radiant_Online_Loading_Oct23.md`. |
| D8.4.2 | Clarified 2026-10-21 (USER): JavaScript defaults to AUTO, including an unset or empty `JS_EXEC_BACKEND`. Supported units start in AST; whole-unit fallback, hot promotion and explicit `mir` use fast MVP MIR. MVP replaces the old MIR engine, not the AUTO tier policy. The redundant `--runtime=mvp-lmd` option remains removed. Performance preservation and remaining effect-analysis gates stay open. Working records: `vibe/jube/JS_MVP_Integration.md`, `vibe/impl/JS_MVP_Integration.md`. |
| D8.4.2 | Ruled 2026-10-20 (USER): preserve fast MVP performance while unifying the default/explicit command and Lambda context, globals, host and lifetime ownership. Implementation and release qualification remain in progress; finer dynamic-effect boundaries remain open. Design and acceptance gates: `vibe/jube/JS_MVP_Integration.md`. |
| D8.4.3 | Revised 2026-10-18: align the retained interpreter with formal MVP direct eval. Both tiers use live `JsInterpEnv` bindings and bound-AST context checks; remove the retired LJS MIR binding-projection journals, source-scan const checks and bridged-eval entry. Lexical receiver payloads use destination-owned Lambda scalar homes in the GC environment. Explicit AST/native and MVP operation profiles remain distinct. Source audit and qualification: `vibe/impl/JS_Interpreter_MVP_Alignment.md`. |
| D8.4.2 | Revised 2026-10-16 (USER): MVP-Lmd is the formal JS MIR engine. Ordinary Script/Module and document/preamble MIR entries are activated; reusable MVP machine-code images keep metadata and relocation tables per realm (D8.5.1). Frontend cache overlays, host/AUTO integration and baseline closure are tracked in `vibe/impl/JS_MVP_Lmd_Formal_MIR.md`. |
| D8.4.5 | Revised 2026-10-03 (USER): the `JS_FUNC_JIT_THRESHOLD` default is 5 calls (100 in v23), the same as Lambda's `LAMBDA_FUNC_JIT_THRESHOLD`. A release sweep of values 1–500 (AUTO, values interleaved, min of three) chose 5 for both languages on end-to-end wall time: Lambda 126 scripts, geomean vs 5 = 1.040 / 1.005 / 1.000 / 1.014 / 1.069 / 1.113 at 1 / 2 / 5 / 10 / 100 / 500; JS 73 rows, vs 100 = 0.983 / 0.978 / 0.984 / 1.000 / 1.008 at 2 / 5 / 10 / 100 / 500. JS at 1 (0.875) is first-call compilation, excluded by hotness-only promotion; its gain comes from rows whose once-called hot function reaches MIR only through a slower loop continuation, a separate defect. `JS_LOOP_JIT_THRESHOLD` stays 10000. Evidence: `vibe/impl/Lambda_Impl_JS_Interp_Tune.md` §11.6. |
| D8.2.1–D8.2.3 | The physical Lambda/JS foundation is substantially shared (`AstNodeType`, many layouts/aliases, `FnAnalysis`, and `MirEmitter`), but structural convergence is incomplete. P1a (2026-08-28) moved Lambda iterator `for` to `AST_NODE_FOR_EXPR`/`AstForNode`; P1b moved Lambda declarations to `AST_NODE_VARIABLE_DECLARATOR`/`AstDeclaratorNode`; P1c folded assignment/declaration-wrapper storage; P1d (2026-08-28) promotes condition loops to one `AST_NODE_LOOP`/`AstLoopControlNode {form, init, test, update, body}` and retires the old while/do/C-style tags, while iterator clauses use `AST_NODE_FOR_CLAUSE`. P1e (2026-08-29) retires the duplicated JavaScript core-child rows and leaves only extension layouts in `js_ast_children.cpp`; No guest acceptance test remains since `lang-python` was removed (D8.2.3). |
| D8.2.4–D8.2.6 | P1–P4 establish the shared indexed compiler substrate described above. P5 (2026-08-30) completes the planned structural semantic-family consolidation: `MirValue` demand/profile lowering now owns shared sequence, condition, module-slot, destination, call/result, return/completion, function-publication, and module-finalization boundaries in Lambda and JS, while profile callbacks retain language semantics and boxed fallback. P6 moves shared context/owner binding, result publication, execution/current-file/module-state scopes, and active-module handles into `runtime-state`; the semantic walkers and their frames remain distinct under **D8.4.2**. The 2026-09-05 FunctionId follow-up retires `FnAnalysis::js_mir_backend`: JS MIR artifacts are reached only through the `func_entries_by_id` table keyed by the shared `FunctionId`; post-order storage remains a backend detail. The 2026-09-23 LC4 closeout adds preorder ranges, reverse query tables, function-child adjacency, synthetic-fragment overlays, named JS collection/capture/environment/inference/forward passes, and the shared MIR link policy. Current verification is 4,098 Lambda/Input and 40,261 Test262 baseline cases, with Lambda 0.512534 and JS 0.690065 release compiler-time ratios. This is an implementation status for **D2.3.4–D2.3.5**, **D6.2.1**, **D6.3.4**, and **D8.2.6**, not a new ruling. Focused gates: `vibe/Lambda_Design_JS_Unified.md` P5–P6. **D8.2.5 (2026-09-11) is historical and superseded operationally by D8.2.5**: `AstNode.type` is the effective-type authority, declared annotations live on their declaring node, and the implemented v3 manager records named pass facts/timings without an ID-keyed per-node fact table. Rationale and the superseded U30 text: `vibe/Lambda_Design_Unified_AST.md` §13; plan: `vibe/Lambda_Proposal_JS_Unify_P7.md` U-D. |
| D7.3.1–D7.3.3, D7.3.4 | **Binder TG8 direct-edge procedure slice implemented 2026-09-16.** Eligible fixed-arity `fn` and `pn` binder functions (task-free only until D7.3.4) with immutable parameters and exact scalar, normalized `array`/`map`/`range`, named/nominal map, or concrete element keys admit at most four immutable `__rawN` bodies in source order. `_b` shares one exact-key matcher: scalar tags, selected type-value identity, normalized container kinds, and descriptor-pointer identity for shape-bearing maps/elements. It retains the complete generic boxed fallback; later, partial, dynamic, capped, or ineligible keys consume no slot. A statically exact local call selects its predeclared `__rawN` body directly; a shape literal is exact when its AST descriptor is the raw key, while declared base-shape and other unproven edges retain `_b`. `var` parameters retain `_b` (task-context and may-await procedures no longer do, D7.3.4). Fixtures: `test/lambda/type_binder_raw_variants.ls`, `test/lambda/proc/type_binder_proc_raw.ls`. [S1.6, D7.3.1–D7.3.4] |
| D5.4.5 | **Ruled 2026-09-16 (user), implemented same day (CW24v3).** The bind-time share-mark of a place copy was gated on `place_copy_mutated` -- whether the COPY is written -- and ignored writes through the PLACE, so an unmutated `let old = r.kid` aliased a later `r.kid.n = 7` on both tiers in every archived release back to Result42 (repros `temp/t28/value_semantics_bug/`). New static fact `NameEntry::place_copy_place_written`, computed at FUNCTION_END per binding from its live range (bind to last top-level statement naming it): root may be written in range (`ast_body_may_write_entry`, the CW30 gate, conservative on unknown callees and `var` passes) or the copy escapes (`ast_body_cow_param_effects` retention). Both tiers' bind gates mark on `mutated || place_written`. Deliberation: `vibe/Lambda_Design_Nested_Mutation.md` §4.3 (CW24v3). Blocks lifted: D5.4.4/CW36 (T28-8). **Conformance fix (same day, T28-8):** `ast_body_may_write_entry` did not look inside declarations, returns, blocks or several composites, so `let old = r.kid; var z = mutate(r)` still printed the new value; it now does. The same helper stopped counting a non-procedure system function or a known plain `pn` parameter as a writer (S9.1.3 keeps the callee's writes local); an unbuilt callee signature stays conservative. |
| D7.3.4 | **Ruled 2026-09-16 (user): `var` parameters and methods leave the boxed-only exclusion list.** O11's reason was that a raw *scalar* carrier severs a borrowed position from the caller's storage; CW33/D8.3.3 later built the home transport that `_b` already uses, so a raw edge that carries the same home preserves the write-back. A method's receiver is admitted on the same exact-shape proof as any other raw argument, never on a dispatch guess. Obligations carried into the implementation: the home is consumed in the prologue and published in the epilogue; S9.1.3 exclusivity and the plain-parameter snapshot are untouched; pointer lanes keep a boxed source root; D7.3.3's two proof paths still hold. Remaining exclusions: closures, variadics, may-await/task-context procedures. Deliberation: `vibe/Lambda_Design_Compiling_Dual_Func.md` §O11v2. Implementation: `vibe/impl/Lambda_Impl_Tune28.md` T28-2. |
| D7.3.4 | **DF16's caller-side guard-hoist slice is implemented, opt-in, 2026-09-15.** `LAMBDA_MIR_TG8_HOIST_GUARDS=1` emits the very `_b` exact-key chain at an eligible local dynamic binder edge; a matching arm calls its immutable `__rawN`, and every miss calls `_b`. Its conservative loop-region case requires bounded raw keys, one unmodified identifier argument, and exactly one eligible dynamic call in a synchronous `while`; it evaluates that shared chain once, prepares the selected raw argument once, then enters its direct-`__rawN` sibling or an `_b`-only fallthrough sibling. Its straight-line CSE case shares a raw-index or boxed selection only between repeated eligible calls to the same callee with the same immutable identifier in one content sequence; both calls remain, while control, assignment, or an intervening different call clears the choice. It excludes closures, methods, `pn`/`var`, variadics, optional/named/spread arguments, and mutable source identifiers. The fixed cap remains four (deterministic source order; no profile feedback); five release benchmark processes measured 0.26 s baseline versus 0.18 s enabled for `test/benchmark/tg8_guard_hoist.ls`. **DF12's scalar first slice is also opt-in:** `LAMBDA_MIR_DF12_SPECULATIVE_LIFT=1` lets bare arithmetic body evidence select an `int` raw shape; the exact wrapper guard admits only `int` and every mismatch runs the complete boxed slow body. Direct float evidence remains `float`; broader speculative shapes await Result-suite measurement. [S1.6, D4.3.2, D7.3.1–D7.3.4, D7.4.1] |
| D7.4.1 | ICs are retired in both lanes: the Lambda lane never had them; the LambdaJS per-site `JsLoadIC`/`JsStoreIC` machinery was deleted 2026-08-15 (IC_Retire IR1–IR8) and the LambdaJS carve-out in LC1 was closed 2026-09-09 (LC1v2). The replacement — compile-predicted literal/constructor shapes with an inline guard, an integer-index lane, and the shared kernel on a miss — is specified in `vibe/jube/JS_Tune_History.md` §4.3 (T10-1/T10-2) and is **not implemented**; Result38 measures LambdaJS at 4.35× QuickJS with the unspecialized kernel path. |
| D7.4.2 | Core direct calls pass individual ABI operands. Internal shape-2 results use two MIR results and C-reachable entries use the context companion slot; the trailing scalar-home operand remains retired. Tune24 aggregate-construction plans carry precise record-field destinations as internal operands and return an Item completion. The numeric companion-return ABI is unchanged. |
| D7.4.3 | Landed 2026-08-17 for Lambda, LambdaJS, Jube, and hosted execution boundaries: ordinary failures use explicit returned completions through each frame, while `LambdaRecoveryFrame` is restricted by the recovery-boundary gate to native-fault/test containment sites. The catalog and adapter audits retain the explicit Item/companion-lane contracts; see `vibe/Lambda_Design_Runtime_Error_Handling.md` §10–§12. |
| D8.5.1 | Partially implemented 2026-09-14: the common source registry is live, Lambda and JS CLI/Node-runner/test-batch/module source admission use it, hosted CLI file admission uses language/profile keys, and Radiant's preamble/external-classic JS MIR adapter stores opaque artifacts there. Closed-T0 Lambda AST templates now instantiate import cones and interpreter view registration through fresh Script overlays; cached Lambda MIR hits instantiate the complete JIT dependency cone and its dense per-runtime slabs, while P2 satellite imports derive their owner/slot without mutating the shared AST. P2 promotion counters and boxed satellite entries are also execution-local overlays, so a cached AST definition cannot expose a retired EvalContext's native entry (D8.3.1); an AST image that has entered P2 is conservatively excluded from later AST hits until lowering's remaining mutable facts have execution overlays. Lambda file-backed dependency changes and admitted cache-safe static-JS ESM dependency changes are refreshed before an artifact hit and retire the common importer cone; exact source comparison decides the retirement. Common AST/MIR build claims single-flight Lambda artifact construction across fresh runtimes, common JS AST claims single-flight parser-template construction, and the Radiant JS-MIR lease adapter plus synchronous static-JS-import module-MIR adapter claim their eligible artifacts; a minimal poison state is fail-closed until source invalidation, while eviction remains deferred. Eager JIT AST-miss reuse remains excluded because MIR lowering still mutates AST facts. The common JS AST adapter supports function, class, module, eval, and TypeScript parser templates through fresh execution overlays; those clones retain the source logical unit and bind a fresh dense slab. An ESM module whose transitive static-JS-import closure is synchronous and cache-safe can reuse an immutable MIR artifact after rebuilding every dependency namespace, its own namespace, and its module slab; TLA, dynamic/re-export, CommonJS, cycles, and cross-language module artifacts remain excluded. The legacy Runtime retention cache is retired to a local `loaded_script_index` registry, and the legacy Radiant cache façade is retired to a bounded lease/accounting session. Open work includes the excluded JS module families, remaining command pipelines, guest build-claim adoption, and cache lifecycle gates; L2 lazy codegen remains approved but `mir.c` is eager. Working record: `vibe/Lambda_Design_Script_Cache.md`. |
| D8.5.1 | Revised 2026-09-14 (v3): a source-level command-path audit closes the executable loader inventory: Lambda run/import/REPL/AST-dump/validator, JS CLI/require/interpreter/module/AST-dump/batch, Radiant local and URL scripts, Jube hosted sources, and direct Bash/Ruby guest loaders all reach `InputScriptCache`; ordinary document, JSON/package-metadata, filesystem, and transfer payload reads remain outside the executable-source contract. An owner scope that closes with an unfinished AST/MIR claim poisons the key and broadcasts its waiters, covering managed parse/compile recovery without inventing a broad crash cache. `LAMBDA_SCRIPT_CACHE_MAX_BYTES` provides an opt-in, inactive-only LRU byte limit; active leases retain their images and report pressure. This supersedes the v2 implementation status above; guest build-claim adoption, broader timeout/crash containment, dependency cones outside the admitted module families, and richer budget policy remain open. Working record: `vibe/Lambda_Design_Script_Cache.md`. |
| D8.3.5 | Revised 2026-09-22 (v5): static closure prebuild is a bounded, dynamic task graph, not serial discovery followed by per-depth worker batches. Canonical profile/path requests deduplicate shared modules; completion continuation releases each direct dependent, and workers do not block on nested imports. The cache retains its process-wide artifact-key single-flight guarantee for separate closures, while module initialization, execution state, and MIR remain runtime-local. Working record: `vibe/Lambda_Design_Script_Cache.md` §12.1. |
| D8.3.4 | Revised 2026-09-22: P2 satellite work uses the same process-wide bounded-work principle but an initial Script-generation/target-definition key. Its artifact is a private immutable image until a receiving execution safe point adopts it; the artifact cache never holds a live EvalContext, module slab, Function pointer, or worker-owned compiler state. Private direct-callee cluster images remain later work. Working record: `vibe/Lambda_Design_Script_Cache.md` §12.1. |
| D8.3.5 | Revised 2026-09-22: static-module prebuild is a process-global future registry, not a closure-owned graph and `wait_all()` barrier. Canonical profile/path requests share one in-flight or completed future across all roots; direct dependency continuations schedule parent AST builds without worker-side waits. Ordinary Lambda/JS import consumption waits only for its direct module future. The `InputScriptCache` remains the exact-source/profile artifact authority, so task readiness never bypasses source invalidation or normal diagnostics. Working record: `vibe/Lambda_Design_Script_Cache.md` §12.1. |
| D8.5.2–D8.5.3 | L3 code-image cache: nothing landed (D0–D6 sequence); de-pointering (MC4) independently shippable, not started. |
| D8.6.4 | Timing/MIR instrumentation is landed. At commit `44b98dcebd19a548a14bbb75785091b545445f00`, the governed tree is 310,711 lines. The audited atomic direct-frontend retirement `9f3f05e1ff65a2c42acf14776da7361ea1961c0c` is `+1,366/-8,450 = -7,084` in `lambda/runtime` + `lambda/js`; its named deleted files alone credit `-6,204`, excluding the out-of-scope TypeScript deletion. The current checker reports 287,618 against the stricter ≤308,711 cap. The 2026-09-05 prescribed captures (one warm-up, five release samples, identical manifests) compare the pre-bind base with the same two semantic repairs applied to both trees: Lambda compiler median ratio `0.512534` and JS ratio `0.690065`, satisfying the ≤0.90 and ≤0.80 ratchets. Finalized JS MIR diagnostics are complete `1.000181` and library `0.999995`; the complete-corpus change is below 0.02% and the library decreased. Large-library and complete-corpus MIR counts remain required diagnostics, not exit gates. |

**D3.5.1 — MVP with dictionaries, 2026-10-02.** Interpreter/MIR file
construction selects `VirtualOutputElement`/`PDFBuilder`; raw byte appends,
dictionary encoding, and `output` are implemented.
The builder owns no Item edges and is released by the existing GC external
payload hook. Producer-return lists and retained-content contracts remain DO32.

**D3.5.1 / D8.2.6 — pure output demand, 2026-10-15.** Both tiers forward
eligible pure result paths to an explicit rooted destination with checked
completion and handler checkpoints/rewind. Counting and emitting share the
encoder; Lambda freezes stream payloads, measures object lengths and emits
without a document-sized fragment Item array. Generic/constrained calls can
materialize; full helper and production-export qualification remains DO32.
[Phase 2 §13](Lambda_Impl_PDF_Output_Phase2.md#13-functional-generation-and-p3p5-continuation--2026-10-15).
[Implementation record](Lambda_Impl_PDF_Output_MVP.md).

## 3. Open-issue history

Numbered `DO#` (design-open); each links to its record.

**Data representation**
- **DO1** Nullable-lane residue: boxed fallback for `any`/`number`/unions
  (confirm); absent-vs-null-valued optional field observability;
  flow-proof scope; native `pn` write-back ABI; ArrayNum re-promotion.
  [Nullable §10]
- **DO25** Node-identity carrier and operations (S5.1.4v2, D3.1.3): where
  the identity lives (a header word costs every container one word; a
  side table keyed by address needs explicit copy on every COW detach; an
  attribute is visible to formatting), the universal id form (document path
  + node id, one scheme for local and online documents), which runtime
  operations copy it, and whether `===` needs a scalar-id fast path.
  Deliberately unruled on 2026-09-03; semantics side is SO39. [OB10]
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
- **DO13** Emission testing: guest-transpiler scope (no guest is hosted
  since `lang-python` was removed, 2026-10-07); MT6 coupling spike; a clean zero-root-elision
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
- **DO33** (numbered DO25 until 2026-11-04, a duplicate ID) Interpreter tier (D8.3.2) remains open for breadth/performance: satellite-module
  treatment under the MT7 emission budgets (AIO2); cross-context visibility of
  promotion cells (AIO8); loop-head handoff breadth — loops outside the
  eligible shape, a second handoff loop per definition, work-weighted
  counting, and a per-Script image budget (AIO13); and the remaining full
  call-site matrix
  needed to widen promotion beyond the current fail-closed boundary (AIO1).
  The release AUTO correctness gate is green; these are follow-on tiering
  breadth/performance questions, not correctness blockers. [Ast_Interpreter
  AIO1–AIO13]

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
  adopter); `dynamic_lookup` retirement (bounded meanwhile by the D9.4.6
  allowlist, which Windows hosts do not yet apply); the Radiant-domain host API and
  the Lambda↔Radiant embed contract; remaining IO policy details (gate taxonomy,
  realms, audit format and platform profiles; app/script defaults, settings,
  and update authority are ruled in D11.2.1–D11.2.4); T2 marshaled interface +
  per-platform sandbox profiles; JS semantic-adapter items 1–8 before POC-1 step 3;
  owning-vs-non-owning `JubeTypeDef`; Windows loading parity. (The ten
  Lang_Hosting §17 follow-up ADRs lapsed with hosted-language support,
  removed 2026-10-07.)
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
  doc (` §2-types` is its designated home).
- **DO23** Shape system opens: cross-`Input` type sharing — each `Input` grows its own transition tree (D3.4.3) — and shape serialization. *Closed 2026-09-25 with the shape pool's retirement:* the pool's hash-collision policy, its element-name-as-attribute-zero encoding, parse-vs-runtime interning (Shape_Pool Phase 5) and `CachedShape.ref_count`; `byte_offset`'s exclusion from identity is stated in D3.4.2.
- **DO24** Unnamed wide temporaries crossing a loop back edge, under
  D6.2.3's reclaim. **D6.2.2 settles the named case**: a wide-capable
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
- **DO26** The D3.1.1 migration, **updated after phase 1 landed and phase 2
  began, 2026-09-03**. The phase-2 flip is site-classified: ~150 sites alias
  MAP and ~15 alias ELEMENT (both just lose their object arm once the value IS
  a map or element); ~35 group all three; ~40 are genuinely nominal (method
  resolution, literal construction, `type()`/`name()`, printing, formatting,
  the MIR method prologue) and must read the record; and ~15 assume the TypeId's
  numeric position (`total_type_rank`, `type_info`, `get_type_name`, the
  contiguous RANGE..OBJECT band checks in `lambda-data.cpp`, `mark_editor`,
  `transpile-mir`, and `dom_element`). The ordering band is a switch, not a
  table: `total_type_rank` in `lambda-eval.cpp` returns 10 for object between
  map's 9 and element's 11, and must key on the record instead. Settled: `ArrayNum` joined the hierarchy in phase 1 and shares
  `List`'s header exactly; the raw-offset audit found sites only in the GC, the
  JIT, and `mark_builder`'s inline-data allocations — `lambda/dom/`,
  `radiant/`, `lambda/input/` and the JS MIR lowering use member access
  throughout. **Closed by USER ruling 2026-09-03: the array-header growth needs no
  benchmark gate** — *"no need to measure/benchmark, this will be our container
  layout."* Post-change release timings kept only for the record (best of three,
  seconds): array1 0.16, quicksort 0.12, gcbench 2.75, richards 3.80,
  mandelbrot 3.76. The `extra` JS-properties slot and `has_js_props` are also
  retired, so phase 1 is complete and bit 6 is free for `is_nominal`. Nothing
  remains open in this entry; phase 2 is tracked by the D3.1.1 phase-2 row.
  [OB20, OB22]
- **DO27** Representation of instance type alteration (S2.1.5): whether
  reconstruction reuses the old buffer when the declared prefix is a
  compatible superset, and how extras are carried. Undesigned. [OB18]

- **DO28** Tier carrier divergence for annotated module bindings: the eager
  module init keeps a trusted `let T: string[] = [...]` literal as a boxed
  Array-of-Items, while T0's declaration boundary (`lambda_type_check` via
  `interp_coerce_declared_array`) rebuilds a pointer-lane element contract
  into its native carrier. Unobservable within a tier (P6), it surfaced when a
  satellite decoded the T0 slab value with the eager assumption (base642 on
  the auto tier read every table element as null). The read now takes the
  generic path under a satellite; the representation choice itself is open --
  one carrier per declared contract at the shared boundary would retire the
  guard (D3.2.2, D4.2.1).
- **DO29** Typed `var` parameter rebind on the eager tier. D8.3.3 keeps
  T0 semantics on the auto tier by pinning a body that rebinds a typed `var`
  parameter (and its direct satellite callers), because the raw entry has no
  home to publish through. The eager whole-module tier (`LAMBDA_EXEC_BACKEND=jit`)
  has the same gap on its direct native edge -- the caller passes the raw
  container and never reloads, so `pn rebind(var x: float[]) { x = fill(2,
  v) }` leaves the caller's binding unchanged where T0 publishes the new
  array (fixture `test/lambda/proc/interp_typed_var_rebind.ls`, golden is
  T0's; the auto tier matches it). Closing it is CW33's typed
  `Container**` half on the raw entry (COW doc §11.10), not a satellite
  matter.
  **Partial implementation, 2026-09-10:** eager typed-array binding arguments
  now transport/reload precise root-slot homes, including wrapper forwarding
  and COW detach after nested capture; the cited rebind fixture passes eager
  JIT too. Typed-record, arbitrary-place and async rebinding remain outside
  this increment, and satellite eligibility restrictions are unchanged.
  See `vibe/impl/Lambda_Impl_Typed_Array.md` §14 for scope and verification.
  **Closed for scalar and record rebinds, 2026-09-14 (D8.3.3):** the
  eager tier had no home at all for a typed non-array `var` position --
  `pn inc(var n: int) { n = n + 1 }` rebound only the callee's lane register
  (every scalar lane, `string`, records and their optional forms; T0 and the
  auto tier were right). Every publishable `var` position now consumes a home
  and publishes its final value through the declared contract lane, the `_b`
  adapter forwards the cell to the raw body, and the auto-tier rebind pin is
  gone (fixture `test/lambda/proc/cow_var_typed_rebind.ls`). Still open:
  arbitrary-place (`f(var m.rows[i])` is a CW25 leaf borrow, not a home) and
  async rebinding, and a scalar-lane binding passed to an *untyped* `var`
  parameter, which would need a mid-function carrier switch (D2.4.3) and so
  keeps the in-place-only behaviour.
- **DO30** *(closed 2026-09-07 — misattributed)* "Eager inference slower
  than boxed bodies on splay." The 256 ms auto figure that the cluster
  (446 ms, the eager tier's own time) seemed to lose came from the
  one-definition satellites' run under-tracing the tree: their `right`
  links sat in `null`-typed lanes at offset 17 and were freed while linked
  (collections traced 17k of 420k objects), so the collector's marks were
  cheap and wrong. The inference side was tested and rejected: not
  counting a comparison against an untyped member read as numeric use kept
  splay's `key` boxed, changed nothing on the eager tier (1.03) and cost
  binarytrees 1.24x and gcbench 1.26x. The defect is D5.3.3's rule; with it
  every tier traces the whole tree and the auto tier runs splay at the
  eager tier's time by right.
- **DO31** Whether an object declaration may name an element tag — `type <B> { x: int; string* }`, B being both the type name and the tag — and how a hierarchy's tags would relate. Deferred 2026-09-25: a nominal name is the type's name only and is never matched against a tag (D3.1.1); a tag is enforced by a structural element pattern, `type e = <tag …>`. [Type_Object OB23]

- **DO32** Virtual PDF output follow-up: extend pure-producer demand coverage,
  retained-child representation (SO48), remaining object encodings, phase-2
  native helpers and production Radiant integration. Explicit pure-result
  demand, shared counting/encoding, byte accounting and expression rollback
  are ruled in D3.5.1/D8.2.6; general value calls may still materialize.
  [PDF Output §5.6](../Lambda_Design_PDF_Output.md#56-what-the-virtual-list-resolves-and-what-remains)

## 4. How the 61.0.0 rewrite was done

**Goals (USER, 2026-11-04).** Rewrite the spec in ASD-STE100 style; make it
concise and well structured; move implementation detail and history out; keep
the principles, because principles stay while details change.

**Structure.** The eight old sections became eleven. Container rulings left
"Data Representation" for a new D3; document formats and shipped document
packages moved to D10; trust, IO policy, app safety and media capabilities
moved to D11; dual-function compiling and the call ABI joined function values
in D7. Each section now opens with its principle, and each ruling title states
a principle as a sentence. The whole document shrank from 326 KB to 127 KB;
the ruling body from about 181 KB to about 107 KB.

**Splits and merges.** The two rulings that had grown by in-place revisions
were split by content: ex-D8.1.1 (18 versions) into D8.3.1–D8.3.6, and
ex-D8.1.3 (43 versions) into D8.4.1–D8.4.6. Twelve pairs of overlapping rulings
were merged (see the Note column of §1). No ruling was dropped: every old ID
maps to a new one.

**What moved out of the spec.**

- Implementation footnotes and open-issue deliberation: §2 and §3 here.
- Detailed design text: the full old text of 84 rulings, appended verbatim to
  45 vibe design records under "Formal Design detail (relocated 2026-11-04)".
  Each block has the heading `ex-<old ID> → <new ID>`.
- Tuning values (promotion and loop thresholds, size limits), environment
  knob names, exclusion manifests (the `lambda-wasm` list), admission lists
  (the LambdaJS promotion slices v13–v42), header byte layouts and measured
  numbers. They remain in the relocated text and in the vibe records.

**Citation migration.** A one-shot script (`utils/formal_design_renumber.py`)
rewrote 20,516 old-ID citations in 1,869 tracked files (Markdown, C/C++,
Lambda, JS, Python, JSON, TSV, `.mir-check`, SVG/Mermaid and the Makefile),
and 237 citations in this document. It skipped the new spec, the generated
index and vendored code. At the user's request it then also migrated
`lambda/mir/VENDOR.md`, a Lambda-authored record in the vendored MIR
directory. The script was not idempotent, because a second run reads new IDs
as old ones. Thus it was deleted before the change was committed, and the §1
tables became the only map. Two citations named proposed rulings that did not exist, so the
script left them: ex-`D5.4.5` in `vibe/Lambda_Design_JS_Interpreter.md` and
ex-`D7.1.8` in `vibe/Lambda_Design_Mobile.md`. They were edited by hand to
D6.4.5 and D9.2.4, the IDs those proposals take in the new outline.

**Conflicts found during the rewrite.** Doc_Convention §2 asks for these to
go to the user; none was resolved by inventing a ruling.

1. Ex-D5.1.3 said "an async frame is a tail-bearing heap container".
   Ex-D5.1.1v3 (2026-10-05) retired the heap async frame. New D6.1.3 keeps only
   "suspension is a re-homing barrier".
2. Ex-D3.4.1 said that every shape field is 8-byte aligned and that an `any`
   field is a 16-byte `TypedItem`. Ex-D2.6.1v3 (minimum field width) and
   ex-D4.3.4 (a nine-byte `any` lane) contradict it. New D3.4.1 states no widths.
3. Ex-D8.1.1 called Tree-sitter "an explicit reference/rollback parser";
   AGENTS.md says the Tree-sitter grammar is kept only for the `lambda-cst`
   verifier. New D8.1.1 lists reference, editor/binding and verifier roles
   only.
4. D3.1.3 / DO25 (node identity): `vibe/Lambda_Design_Reference.md`
   (PROPOSED) and `vibe/impl/Lambda_Impl_Reference.md` describe a D3.1.3v2
   that closes DO25, and the semantics spec already has S5.1.4v3. The design
   spec never received D3.1.3v2. The rewrite keeps D3.1.3 and DO25 as they
   were.
5. Two different issues were both numbered DO25. The interpreter-tier entry
   became DO33, and its two impl records were updated.
6. D2.3.3 (ex-D2.3.2): its footnote already said that the ruling text waits
   for the user, because the unboxing helpers are unchecked casts.


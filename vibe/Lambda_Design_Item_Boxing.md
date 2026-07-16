# Lambda Item Boxing Design

- **Status:** CURRENT DESIGN RECORD
- **Date:** 2026-07-10
- **Scope:** the 64-bit `Item` representation shared by Lambda core, LambdaJS, the Jube frontends, Radiant, the MIR transpilers, and the garbage collector.
- **Decision:** retain Lambda's hybrid representation: inline tagged values and tagged leaf pointers for scalar types; unmodified native pointers for header-bearing containers.
- **Convention:** source references name symbols rather than fixed line numbers because line numbers drift.
- **Related:** `Lambda_Type_Double_Boxing.md` (proposed inline-double extension), `Lambda_Transpile_Restructure6.md` (measured direct-string-pointer experiment), `Lambda_Semantics_Number_Model.md`, `doc/dev/lambda/LR_03_Value_and_Type_Model.md`, and `doc/dev/js/JS_03_Value_Model.md`.

---

## 0. Executive Summary

Lambda represents every dynamically typed value in one 64-bit `Item`, but it deliberately does **not** use one physical encoding for every kind of value.

There are three principal storage classes:

| Storage class | Representative values | Where the type comes from | Payload access |
|---|---|---|---|
| Inline tagged value | null, bool, compact `int`, sized numerics | high byte of the `Item` | directly from Item bits |
| Tagged leaf pointer | string, symbol, binary, datetime, decimal, int64, current boxed float | high byte of the `Item` | extract the low 56-bit pointer, then dereference |
| Raw header pointer | array, numeric array, map, object, element, range, path, function, type | first byte of the pointed-to object | the Item word is already the native pointer |

The asymmetry is intentional:

- **Scalars are type-first.** Generic runtime code frequently asks what scalar type it has before reading the payload. Keeping the `TypeId` in the Item makes that dispatch register-only and lets compact leaf allocations omit a type header.
- **Containers are data-first.** Once code has an array, map, object, or element, it usually dereferences it immediately. Keeping the native pointer unchanged removes untagging from every field access and lets the C runtime and JIT pass the same word directly as `Array*`, `Map*`, and related ABI types.
- **Container families are open behind the pointer.** Header metadata such as `Container.type_id`, `MapKind`, shapes, and nominal type objects can refine behavior without requiring a distinct high-byte Item encoding for every concrete container subtype.

The runtime presents one semantic type interface, `get_type_id(Item)`, over these different physical representations. The design should therefore be unified at the **API and invariant** level, not forced into one bit-level encoding.

Lambda tested the most plausible unification alternative: changing String/Symbol/Binary from tagged pointers to raw header pointers. The release benchmark experiment regressed the geometric mean by **11.1%**, with 55 of 62 benchmarks slower. Type tests were much more frequent than boxing/unboxing, so the added header load dominated the removed pointer-tag operations. This result strongly supports the current hybrid.

---

## 1. Goals and Terminology

### 1.1 Goals

The Item representation is designed to provide:

1. one word per dynamic value;
2. cheap type dispatch for heterogeneous scalar code;
3. allocation-free common immediates;
4. direct native-pointer access for aggregate data;
5. a stable ABI across Lambda core, LambdaJS, Radiant, and Jube;
6. enough semantic type capacity for Lambda's richer value domain;
7. precise classification for GC tracing and JIT rooting;
8. a representation that can evolve locally, as with the proposed inline-double extension.

### 1.2 Terminology

- **Boxing** means converting a native value or pointer into the universal 64-bit `Item` representation. It does not necessarily imply allocation: packing an `int` or casting a raw `Array*` into an Item are both boxing operations at the ABI boundary.
- **Unboxing** means recovering a native scalar or pointer from an Item.
- **Immediate** means the payload lives directly inside the Item word.
- **Tagged leaf pointer** means bits 63–56 contain a `TypeId` and bits 55–0 carry a pointer to a scalar payload object.
- **Raw header pointer** means the Item word is the unmodified native pointer, and the pointed-to object begins with a `TypeId`.
- **Semantic type** is the user/runtime-visible `TypeId`. **Storage class** is how that value is physically encoded. They are related but not identical.

---

## 2. Prior Art

### 2.1 NaN-boxing

IEEE-754 binary64 reserves a large family of NaN bit patterns: exponent `0x7FF` with a non-zero fraction. A language generally needs only one semantic NaN, so a NaN-boxed runtime uses the remaining NaN payload patterns for non-double values.

A typical 64-bit NaN-boxed word is interpreted as:

```text
ordinary IEEE number or infinity  -> double
canonical NaN pattern             -> language NaN
selected NaN payload patterns     -> tag + pointer/int/bool/null payload
```

SpiderMonkey's 64-bit packed representation stores doubles directly, uses a tag field adjacent to the exponent for other values, restricts GC pointers to a low address range, and canonicalizes NaNs that could collide with non-number encodings. JavaScriptCore uses a related NaN-space representation and transforms encoded doubles by a fixed offset.

#### Advantages

- Every ordinary double, zero, infinity, and canonical NaN fits directly in the universal value word.
- No HeapNumber allocation is required merely because a double crosses a dynamic-value boundary.
- A JavaScript-sized taxonomy fits efficiently into eight bytes.
- Number predicates can be made especially cheap by ordering tag ranges carefully.

#### Costs

- Pointer payload width and address placement are constrained; SpiderMonkey's documented 64-bit representation relies on 47-bit GC pointers.
- The available clean tag space is much smaller than a full high byte.
- Integers are typically limited to int32-class immediates rather than Lambda's compact safe-integer band.
- Non-number encodings, pointer extraction, NaN canonicalization, GC classification, and equality/hashing all become coupled to the NaN layout.
- A migration changes the universal representation of every value category at once.

NaN-boxing is an excellent fit for a JavaScript-only VM whose dominant universal scalar is binary64. Lambda has a broader scalar taxonomy and an established raw-container ABI, so adopting it would trade away several existing strengths to solve primarily the double case.

### 2.2 V8: low-bit tags, Smis, HeapNumbers, and pointer compression

V8 does not NaN-box its universal JavaScript value. Its `Tagged<T>` representation uses low alignment bits:

```text
Smi                          -> immediate small integer, low tag 0
strong heap-object reference -> pointer with low tag 01
weak heap-object reference   -> pointer with low tag 11
```

Numbers outside the Smi range are represented by heap objects when they need the universal tagged form. In modern V8, optimized code avoids much of that cost through type feedback and specialization:

- numeric values remain in native integer or floating-point registers;
- object representations can specialize numeric fields or use owned mutable `HeapNumber` cells;
- arrays can use specialized numeric element kinds;
- mutable heap-number slots avoid repeated allocation in selected contexts.

On common 64-bit builds V8 also compresses heap references. Heap fields store a 32-bit tagged offset inside a heap cage, and loads reconstruct the execution pointer by adding the cage base. The optimizer removes or hoists redundant decompressions, while the denser representation improves memory bandwidth and cache use.

#### Advantages

- Small integers are immediate.
- Pointer tagging uses alignment bits rather than reducing a high-byte taxonomy.
- Pointer compression halves the storage used by many heap references.
- A strong feedback JIT keeps many numbers and pointers out of the generic representation entirely.
- Low-bit tag correction can often be folded into a field-load displacement rather than emitted as a separate instruction.

#### Costs

- A normal V8 heap-object value is not a raw native pointer; it has a low tag, and compressed references require cage-base reconstruction after a heap load.
- The compressed heap is constrained by its cage model.
- Non-Smi numbers require HeapNumber representation when specialization does not eliminate the universal boundary.
- The design relies heavily on feedback-driven specialization to keep generic representation costs off hot paths.

V8 demonstrates that a runtime does not need NaN-boxing to be fast, but it also demonstrates that value representation must be considered together with element kinds, typed fields, feedback, memory density, and JIT specialization.

---

## 3. Lambda's 64-bit Item Representation

The authoritative definitions are `EnumTypeId` and the C boxing helpers in `lambda/lambda.h`, plus the C++ `Item` union and accessors in `lambda/lambda.hpp`.

### 3.1 Physical layouts

#### Inline tagged payload

```text
63                       56 55                                  0
+--------------------------+-------------------------------------+
| TypeId                   | inline payload                      |
+--------------------------+-------------------------------------+
```

Examples:

- `ITEM_NULL`, JavaScript undefined, TDZ, and other sentinels;
- bool;
- compact `int`, restricted to the exact binary64 safe-integer band;
- `NUM_SIZED`, whose low payload also includes the sized-numeric subtype and raw f16/f32 or integer bits.

#### Tagged leaf pointer

```text
63                       56 55                                  0
+--------------------------+-------------------------------------+
| TypeId                   | low 56 bits of payload pointer      |
+--------------------------+-------------------------------------+
```

The constructors `l2it`, `u2it`, `d2it`, `f642it`, `c2it`, `k2it`, `y2it`, `s2it`, `x2it`, and `err2it` encode this family. C++ Item bitfields such as `string_ptr`, `symbol_ptr`, and `datetime_ptr` extract the pointer payload.

Representative types:

- int64 and uint64;
- current boxed float/f64;
- decimal and BigInt carrier;
- datetime;
- symbol, string, and binary;
- structured error pointer.

These leaf structs do not all share a header. For example, `String` begins with `len` and `is_ascii`, while `Symbol` begins with `len` and a namespace pointer. The Item tag supplies their type without increasing every leaf allocation.

#### Raw header pointer

```text
63                                                              0
+----------------------------------------------------------------+
| unmodified native pointer                                      |
+----------------------------------------------------------------+
                         |
                         v
             +--------------------------+
offset 0     | TypeId                   |
             +--------------------------+
             | flags / kind / fields... |
             +--------------------------+
```

The `p2it` helper stores the pointer unchanged. The C `it2map`, `it2list`, `it2arr`, `it2obj`, `it2elmt`, `it2range`, `it2path`, and `it2p` helpers are direct casts; the C++ versions return the matching Item union field.

Representative types:

- range;
- generic and numeric arrays;
- map and VMap;
- element and object;
- type and function values;
- path.

Each header-bearing object starts with a semantic `TypeId`. Maps, objects, and elements add `MapKind`, shape/type metadata, or both, allowing many concrete behaviors behind a small number of top-level Item/container families.

### 3.2 Normalized type discovery

Today `Item::type_id()` implements the hybrid classifier:

```cpp
if (_type_id != 0) return _type_id;          // inline or tagged leaf
if (item != 0) return *(TypeId*)item;        // raw header pointer
return LMD_TYPE_NULL;
```

The proposed inline-double design prepends its float self-tag test, but it preserves the raw-container branch. See `Lambda_Type_Double_Boxing.md` for the double-specific encoding, special values, equality rules, migration audit, and benchmark gates.

The important abstraction is:

> `get_type_id(Item)` returns the semantic type regardless of where that type is physically encoded.

Code that operates on a dynamic Item should use this normalized interface. Direct `_type_id` reads are representation-sensitive and must be limited to code that has proved its storage class.

### 3.3 Container construction and access

Raw containers are not tagged during boxing:

```c
static inline Item p2it(void* ptr) {
    if (!ptr) return ITEM_NULL;
    return (Item)(uint64_t)(uintptr_t)ptr;
}
```

Known-type access is an identity conversion:

```c
Array* arr = it2arr(item);
int64_t length = arr->length;
```

The MIR transpiler preserves this representation in typed fields: container slots contain raw `Container*`, and field reads return the pointer unchanged. LambdaJS typed-array and regular-array fast paths likewise treat the Item as the native `Map*` or `Array*` once guards have established its type.

### 3.4 Native representations outside Item

Item boxing is not the only runtime representation:

- statically known floats can remain `MIR_T_D` registers;
- pointer-producing runtime calls use the semantic `MIR_T_P` ABI even though backend registers physically carry pointer-sized values as integers;
- shaped numeric fields store native integers or doubles directly;
- `ArrayNum` stores homogeneous numeric lanes directly;
- typed arrays store their specified element representation in backing memory.

The universal Item is therefore a boundary representation, not a requirement that every hot value repeatedly box and unbox.

---

## 4. Why Lambda Keeps Two Pointer Schemes

### 4.1 Tagged leaf pointers optimize type-first scalar use

Generic scalar operations frequently perform type discrimination without immediately consuming the payload:

- equality and ordering select numeric/string/symbol paths;
- conversion and formatting switch on the source type;
- validators compare actual and expected scalar types;
- property keys distinguish int, string, and symbol;
- GC classifies leaf pointers;
- multi-frontend bridges normalize values through the shared runtime.

For a tagged leaf, this classification is register-only. The pointer extraction cost is paid only after the runtime has selected the matching scalar operation.

Tagged leaves also keep scalar objects compact. A String does not need a universal heap-object header merely to say "string"; the Item already carries that information.

### 4.2 Raw header pointers optimize data-first aggregate use

Aggregate code normally wants the object itself:

- `arr->length`, `arr->items`, or numeric lane data;
- `map->type`, `map->data`, and `map_kind`;
- `element->items`, attributes, and shape data;
- `function->ptr` and closure environment;
- `path->parent` and segment metadata.

Keeping the pointer unchanged gives:

- no mask, subtraction, or cage-base reconstruction before dereference;
- direct compatibility with C/C++ runtime signatures;
- simpler MIR pointer passing;
- no duplicate Item tag that must agree with the object header;
- a natural home for subtype metadata that exceeds the useful Item-tag taxonomy.

When dynamic code first reads `Container.type_id` and then accesses fields, the header load often warms the same cache line needed by the following operation. Known-type code skips the type read entirely and dereferences the raw pointer directly.

### 4.3 Measured evidence: the direct-string-pointer experiment

`Lambda_Transpile_Restructure6.md` records an implemented release-build experiment that converted String/Symbol/Binary to the raw-header-pointer model.

The hypothesis was that removing an OR during boxing and a 56-bit extraction during unboxing would speed up string-heavy workloads. The result was the opposite:

| Metric | Direct-pointer result vs tagged-pointer baseline |
|---|---:|
| Geometric mean | **11.1% slower** |
| Benchmarks slower by more than 3% | **55 / 62** |
| Benchmarks faster by more than 3% | 2 / 62 |
| `base64` | 20.1% slower |
| `levenshtein` | 16.5% slower |
| `revcomp` | 14.8% slower |
| `knucleotide` | 17.4% slower |

The root cause was frequency: type checks occurred far more often than string boxing operations. Replacing a register tag read with a branch and header load overwhelmed the instruction saved during pointer boxing/unboxing. The experiment also exposed seven branch-specific correctness hazards caused by changing which values could safely use direct `_type_id` checks.

This is Lambda-specific evidence, not merely a theoretical preference: leaf scalars should retain their high-byte type tags.

### 4.4 Why not tag every container pointer?

Giving each container an Item high-byte tag would make its broad type visible without a header load, but every access would first reconstruct the pointer:

```text
extract TypeId
mask high byte from pointer
load container field
```

The mask is a dependency before the load and cannot generally be folded into an address displacement as a low alignment tag can. It would also duplicate `Container.type_id`, consume valuable high-byte encodings, and require canonical agreement between the Item and header.

Using one generic `CONTAINER` high-byte tag is strictly worse for Lambda's current layout: code would mask the pointer and still read the header to distinguish array, map, object, element, and other families.

### 4.5 Why not make every heap value a raw pointer?

A universal raw-header model would require every scalar allocation to start with compatible type metadata. It would:

- add a memory load to every scalar type query;
- enlarge compact leaf headers such as String;
- make empty/invalid pointer handling more dangerous;
- push heterogeneous scalar dispatch toward pointer chasing;
- invalidate extensive code that relies on direct high-byte scalar classification;
- reproduce the measured direct-string-pointer regression across more types.

The removed pointer-tag operations are too cheap to compensate for the added type loads in Lambda's observed workloads.

### 4.6 Decision

> Keep the hybrid: tagged leaves, raw aggregates, inline immediates.

Representation uniformity is not a goal by itself. Each storage class should optimize its dominant operation while the public Item API provides semantic uniformity.

---

## 5. Container Extensibility

Raw container pointers avoid spending an Item high-byte pattern on every concrete aggregate kind, but they do not create unlimited top-level `TypeId` values by themselves. `TypeId` remains an 8-bit semantic identifier, and current metadata tables assume a mostly dense core enum.

Extensibility instead comes from layered metadata:

```text
Item storage class
  -> broad Container.type_id family
      -> MapKind / flags
          -> shape, nominal type, vtable, or resource metadata
```

Examples include:

- plain maps, typed arrays, ArrayBuffers, proxies, descriptor maps, and sparse-array companion maps behind the Map family;
- nominal objects distinguished by their type metadata;
- VMaps dispatching through a vtable;
- Elements combining list content with shaped attributes;
- Paths distinguished by scheme and segment flags.

New concrete behaviors should prefer an existing broad container family plus header metadata. A new top-level `TypeId` is appropriate only when the value has genuinely distinct language semantics that generic runtime dispatch must recognize.

---

## 6. Runtime and GC Invariants

The hybrid design depends on explicit invariants.

### 6.1 Word and address invariants

1. `sizeof(Item) == 8` and an Item is passed by value.
2. Tagged leaf pointers must fit in the low 56 bits used by the Item payload.
3. Raw container pointers must be returned in the supported low user address range so their high-byte classification is not mistaken for a scalar tag.
4. Under the proposed inline-double scheme, raw pointers must also remain outside the double discriminator space.
5. Pointer-tagging facilities such as AArch64 TBI/MTE must not silently introduce top-byte metadata into Lambda Items unless the representation layer explicitly strips or supports it.

These assumptions are pragmatically valid on Lambda's supported mainstream configurations, but they should be documented and checked in debug/runtime allocation paths so an unsupported address model fails loudly rather than corrupting type classification.

### 6.2 Header invariants

1. Every raw Item object begins with a valid `TypeId` at byte offset zero.
2. C and C++ definitions of container layouts must agree.
3. Internal header-compatible objects, such as accessor pairs that intentionally present as Function, must be guarded by their owning metadata before consumers use a more specific layout.
4. `Container.type_id` is the semantic family; secondary fields refine storage and behavior.

### 6.3 Construction invariants

1. Each value has one canonical Item encoding within a build.
2. Container constructors return raw pointers; code must not OR a container `TypeId` into them.
3. Tagged leaf constructors must use the canonical `*2it` helper rather than manual constants.
4. Null pointers become the canonical null Item, never an all-zero raw pointer masquerading as a container.
5. Proposed self-tagged doubles must use the canonical float encoder; details belong to `Lambda_Type_Double_Boxing.md`.

### 6.4 Access invariants

1. Dynamic code obtains semantic types through `get_type_id()` / `Item::type_id()`.
2. Known container code may use identity-cast accessors such as `it2arr` and `it2map` after a static proof or runtime guard.
3. Tagged leaf access goes through type-specific accessors that remove the tag.
4. Raw Item equality is not semantic equality unless the type's canonical representation proves that it is safe.
5. Direct `_type_id`, `>> 56`, or pointer-mask logic is representation-layer code and must be audited whenever an encoding changes.

### 6.5 GC and MIR invariants

- GC pointer extraction distinguishes raw containers from tagged leaf pointers before tracing.
- Raw container pointers and other GC-managed pointers may be carried physically in integer MIR registers, but semantic `MIR_T_P` information must survive long enough for call ABI and rooting decisions.
- Raw pointers improve access speed but constrain moving-GC designs: object addresses referenced directly by Items must remain stable or all references must be precisely updated.
- Numeric and shaped storage can have separate relocation/fixup rules; those rules must not infer pointer-ness solely from arbitrary high bytes after new self-tagged encodings are introduced.

---

## 7. Comparison with Prior Art

| Dimension | Lambda hybrid high-byte design | NaN-boxing | V8 tagged/compressed design |
|---|---|---|---|
| Universal word | 64-bit Item | 64-bit value | pointer-sized execution value; commonly 32-bit compressed heap storage |
| Common integers | compact safe-integer-band `int`; sized numeric immediates | typically int32 immediate | 31/32-bit Smi |
| Doubles | currently tagged pointer; proposed self-tagging documented separately | inline by construction | HeapNumber at generic boundary; unboxed in optimized paths/typed storage |
| Scalar type test | direct high-byte read | tag/range test in NaN space | low-bit/tag and object-kind machinery |
| Leaf pointer access | remove high-byte tag | extract/XOR NaN payload tag | remove low tag; decompress after compressed heap load |
| Container pointer access | native pointer, no untagging | pointer reconstructed from payload | low tag; commonly compressed in heap fields |
| Exact container type | header byte, then subtype metadata | value tag or object header depending design | object map/header |
| Tag/type capacity | rich high-byte scalar taxonomy plus header subtypes | constrained NaN payload tags | small low-bit storage classes plus heap metadata |
| Pointer/address constraint | tagged leaves use low 56 bits; raw containers require supported low address model | commonly 47–51-bit payload assumptions | low-bit alignment; compressed builds use heap cages |
| Reference density | 8-byte Item references | 8-byte values | often 4-byte compressed tagged references in heap fields |
| Best fit | mixed data/document runtime with rich scalar and container domains | JS-like double-dominant universal values | highly optimizing JS VM with feedback and pointer compression |

### 7.1 Lambda advantages

- Register-only dispatch for a rich scalar taxonomy.
- Compact int band aligned with exact binary64 integers, plus inline sized numerics.
- Native raw-pointer ABI for containers and direct aggregate traversal.
- Container subtyping through headers, kinds, shapes, and nominal metadata rather than Item-tag proliferation.
- Local evolution: the double representation can change without rewriting every pointer and integer encoding.
- One shared representation supports Lambda, LambdaJS, Python/Ruby/Bash frontends, document inputs, validators, and Radiant.

### 7.2 Lambda disadvantages

- More than one physical pointer representation must be audited and understood.
- Dynamic container type discovery requires a header load.
- Tagged leaf dereference requires pointer extraction.
- Raw pointer classification depends on supported virtual-address behavior.
- Eight-byte references sacrifice the memory-density advantage of V8 pointer compression.
- Direct raw pointers constrain moving-GC and sandboxing options.
- The current boxed 64-bit scalar design allocates float/int64/datetime payloads; the float part is addressed by the separate double proposal.
- Representation-sensitive shortcuts such as raw `_type_id` reads and raw Item equality are sharp edges.

### 7.3 Assessed structural weaknesses

The §7.2 list names the costs; four of them deserve deeper treatment because the codebase shows their symptoms. These are weaknesses of the design *as practiced*, assessed against verified code findings (2026-07), not hypotheticals.

**W1 — The invariants were implicit, and the codebase shows the symptoms.** This is the largest weakness, and it is not in the encoding: for most of the design's life, its contract lived in convention rather than in checked code. The accumulated evidence: two JS sentinels squatting in unclaimed tag space, chosen precisely *because* nothing defined what unclaimed tag space means (`JS_DELETED_SENTINEL_VAL`, `JS_ITER_DONE_SENTINEL` — discovered only when the inline-double partition made them collisions); ~24 raw `>> 56` extractions scattered across 11 files; open-coded payload dereferences bypassing the canonical accessors (`Item::get_double`); JIT-emitted raw `MIR_EQ` comparisons on Item words whose semantics silently depend on the representation; and a shipped representation-sensitivity bug in `ArrayNum` equality. None of these indict the hybrid itself — they are the predictable residue of a representation without an enforcement story. The §6 invariants plus the double-boxing Part 7 assertion regime are, in effect, the design belatedly acquiring that story; the guardrail layer (impl plan S0) should be understood as **part of this design**, not as an appendage of the float work.

**W2 — Boxed 64-bit scalars were the under-designed storage class.** The tagged-leaf row of the §0 table works well for strings and symbols but never had a coherent answer for float/int64/datetime: their payloads live in three places with three different lifecycles — a numeric nursery that is *never collected* (`gc_nursery.h`: values persist until `nursery_destroy()`, a monotonic leak in long-running processes), GC-heap leaf objects, and payloads embedded in container buffer extra areas with their own relocation fixups. It took benchmark-defining regressions (nbody-class, >150× vs Node) to force the float fix (`Lambda_Type_Double_Boxing.md`), and int64/datetime still carry the leak. Pointer-distinct equal values from this class are also the root of the recurring raw-equality bug family. The row is being repaired value-by-value; the lifecycle story for the remainder belongs to the nursery redesign.

**W3 — Raw container pointers are a permanent strategic commitment.** Items holding bare addresses means the object heap can never move (or every reference must be precisely updated — in practice: never move), which shaped and constrained the entire GC-evolution space (sticky/pinning designs instead of copying collection), and forecloses V8-style pointer compression — Lambda pays 8-byte references and the cache pressure that follows in pointer-heavy workloads. It also means a garbage word with a zero high byte is a *wild dereference* in `type_id()`, so memory safety leans entirely on canonical construction (§6.3). These trades are defensible for a C-native multi-frontend runtime and this document accepts them deliberately — but they are one-way doors, and should be re-acknowledged whenever GC or sandboxing work is planned.

**W4 — Dynamic container type discovery pays a header load.** The `tag == 0 → dereference` step in the classifier puts a memory access in exactly the generic code a data-processing language runs constantly — formatters, validators, equality, GC tracing. It is the correct side of the trade (the §4.3 experiment shows the alternative is worse, and data-first access patterns usually want the cache line anyway), but it is a real, recurring cost, and it concentrates in the dispatch-heavy workloads where Lambda already trails.

### 7.4 Overall assessment

NaN-boxing is strongest when all doubles must cross generic boundaries cheaply. V8 is strongest when a feedback JIT and compressed heap can keep generic representation costs away from optimized code. Lambda's hybrid is strongest when one runtime must efficiently support both rich scalar dispatch and native aggregate traversal.

The schemes optimize different constraints. Lambda should borrow V8's typed fields, element kinds, and native-register specialization, and borrow self-tagging ideas for doubles, without discarding the high-byte scalar taxonomy or raw-container ABI that fit Lambda's broader data model.

---

## 8. Evolution Rules

When adding or changing a value type:

1. **Choose by dominant operation, not aesthetic uniformity.** If runtime code usually checks the type before reading a compact leaf payload, prefer a tagged leaf. If it usually traverses a header-bearing aggregate, prefer a raw pointer behind an existing container family.
2. **Do not create a new Item tag for every subtype.** Use `MapKind`, shape metadata, nominal types, or vtables behind a broad semantic family.
3. **Preserve canonical construction.** One value must not alternate between raw and tagged pointer encodings within a build.
4. **Centralize representation-sensitive primitives.** Extend `get_type_id`, canonical boxing helpers, accessors, GC extraction, hashing/equality, and JIT lowering together.
5. **Measure representation migrations.** Type-test frequency, pointer dereference frequency, allocation size, cache behavior, and full release benchmarks matter more than the instruction count of boxing alone.
6. **Keep escape hatches local.** A proposed representation should be feature-gated until baseline, ASan, GC, interpreter, JIT, and benchmark gates agree.

The next four rules are corrective — each answers one of the §7.3 weaknesses:

7. **Enforcement is part of the design (answers W1).** Every representation invariant in §6 must exist as a compile-time assertion, a debug runtime check, or a lint rule — not as prose alone. New tag constants, sentinels, and packed encodings are illegal until pinned by an assertion; new code reading raw high bytes or raw payloads outside the representation layer must fail `make lint`. The guardrail set in `Lambda_Impl_Double_Boxing (done).md` S0 is the reference implementation of this rule; it stands on its own merits regardless of the float migration's fate.
8. **A storage class is not designed until its lifecycle is (answers W2).** A representation decision must state where payloads are allocated, how they are reclaimed, and how equality behaves — "tagged pointer to an 8-byte payload" without a reclamation story produced the never-collected numeric nursery. Any new leaf class (and the remaining int64/datetime repair) must specify all three before landing.
9. **Semantic equality has one entry point (answers W1's sharpest recurrence).** Raw Item bit-comparison is a representation-layer operation (§6.4.4). Runtime and JIT code must obtain value equality through the canonical helpers; every emitted raw-word compare must carry a proof that the operands' types make bit-equality semantic. This bug family has shipped at least three times (ArrayNum `==`, NaN identity, packed −0); the lint rule and the `MIR_EQ` audit are its ratchet.
10. **The tag budget is governed, not discovered (answers W1/W3 at the boundary).** Legal high-byte space is a finite architectural resource — 64 values once inline doubles land. Claiming a value requires updating the partition assertions and this document in the same change; "it was unused" is how the sentinel collisions happened. Re-acknowledge the W3 one-way doors (non-moving heap, no pointer compression, low-address model) whenever GC, sandboxing, or platform-port work is planned, rather than rediscovering them mid-design.

The current design decision remains:

> **Unify Lambda's value semantics and APIs; retain specialized physical encodings for immediates, scalar leaves, and aggregate containers.**

---

## 9. Source and Prior-Art References

### Lambda sources

- `lambda/lambda.h` — `EnumTypeId`, `Item` boxing macros, `p2it`, C container accessors, and C-visible layouts.
- `lambda/lambda.hpp` — C++ `Item` union, `Item::type_id()`, scalar getters, and direct container accessors.
- `lambda/lambda-path.h` — header-bearing raw `Path` layout.
- `lambda/transpile-mir.cpp` — native scalar registers, semantic pointer ABI, raw container fields, and direct aggregate lowering.
- `lambda/js/js_mir_expression_lowering.cpp` — LambdaJS array and typed-array direct access.
- `lib/gc/gc_heap.c` — GC classification of raw and tagged pointer Items.
- `vibe/Lambda_Transpile_Restructure6.md` — measured negative result from unifying String/Symbol/Binary with raw containers.
- `vibe/Lambda_Type_Double_Boxing.md` — proposed float self-tagging extension and its migration requirements.

### External primary references

- SpiderMonkey, `JS::Value` boxing formats: <https://searchfox.org/firefox-main/source/js/public/Value.h>
- JavaScriptCore, `JSValue` encoding: <https://github.com/WebKit/WebKit/blob/main/Source/JavaScriptCore/runtime/JSCJSValue.h>
- V8, current `Tagged<T>` representation: <https://chromium.googlesource.com/v8/v8.git/+/main/src/objects/tagged.h>
- V8, pointer compression design: <https://v8.dev/blog/pointer-compression>
- V8, Maglev and optimized numeric representation: <https://v8.dev/blog/maglev>
- V8, mutable `HeapNumber` fields: <https://v8.dev/blog/mutable-heap-number>
- V8, specialized double-element arrays: <https://v8.dev/blog/spread-elements>

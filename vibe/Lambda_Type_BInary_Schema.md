# Lambda Binary Schema — Enhancement Proposal

> **Status:** Proposal
> **Scope:** Schema-driven binary (de)serialization for Lambda Script — a typed binary layout language expressed in Lambda's `type` system, a lazy zero-copy virtual element (`Velmt`) backed by a byte buffer + schema, and a schema-driven serializer for the round trip.
> **Builds on:** [Lambda_Type_Binary.md](Lambda_Type_Binary.md) — the `Binary` runtime representation (Tier 1) and sub-binary zero-copy slicing (Tier 3) are prerequisites for the lazy/zero-copy Velmt described here.
> **Prior art:** [Kaitai Struct](https://doc.kaitai.io/) (declarative binary parsing), Erlang bit syntax, Python `construct`, Rust `binread`/`deku`, Protobuf/FlatBuffers (zero-copy access), ASN.1.
> **Related:**
> - [Lambda_Type.md](../doc/Lambda_Type.md) — type hierarchy & annotations
> - [Lambda_Data.md](../doc/Lambda_Data.md) — element/map data model
> - `lambda/lambda.hpp` — `VMap` virtual-map vtable (`Velmt` mirrors this pattern)
> - [Lambda_Validator_Guide.md](../doc/Lambda_Validator_Guide.md) — schema/validator framework

---

## 1. Motivation

[Lambda_Type_Binary.md](Lambda_Type_Binary.md) makes `binary` a real byte buffer with indexing, slicing, and an encoding stdlib. That gives us **bytes**. It does not give us **structure**: every binary format (PNG, ELF, network packets, on-disk records, game saves, sensor frames) is a *typed layout over bytes*, and today a Lambda user must hand-roll `read_u32_be(b, off)` chains to walk one.

Kaitai Struct demonstrated the winning model: describe the layout *once*, declaratively, and get a parser, a navigable object tree, and (with some tools) a serializer for free. Lambda is uniquely positioned to do this *better* than Kaitai because:

1. Lambda already has a rich structural data model — **elements with attributes and ordered children** — which is a natural target for a parsed binary tree. No new object model is needed; we reuse `Element`.
2. Lambda already has a **type system with annotations**. A binary schema is just a `type` with physical-layout annotations. No separate `.ksy` DSL, no codegen step, no foreign toolchain.
3. Lambda's "documents as data" thesis means the same query/transform/format machinery (pipes, `match`, formatters) applies to a parsed binary the instant it becomes an element tree.

This proposal adds three capabilities:

- **§4 Schema in the type system** — express binary layouts as annotated Lambda `type`s (the user's `animal_record` example).
- **§5 `Velmt`** — a lazy, zero-copy virtual element that presents a packed buffer + schema through the standard Lambda element API.
- **§6 Schema-driven serialization** — walk a `Velmt` (or any element conforming to the schema) and emit packed bytes.

---

## 2. Design Decisions (resolved)

| Decision | Choice | Rationale |
|---|---|---|
| **Schema syntax** | **Reuse `type` + field annotations.** Logical shape is a normal Lambda `type`; physical layout rides on annotations (`uint16/be`, `uint8[16]`, `@offset`, `if`, `repeat`). | Zero new top-level grammar; schemas are first-class Lambda values, composable, importable, and usable by the existing validator. |
| **Endianness** | **Declared once at the schema root, inherited by all nested fields, overridable per field.** Mirrors Kaitai's `meta/endian`. | Matches real formats (a format is almost always single-endian); avoids per-field verbosity while staying explicit where it matters. |
| **Velmt decoding** | **Lazy + zero-copy.** `Velmt` holds `{ buffer, schema, base_offset }`; scalar/array fields decode on access; sub-structs and `binary`/array slices are zero-copy sub-views into the parent buffer. | Handles multi-GB / mmap'd inputs; depends on sub-binary slicing from [Lambda_Type_Binary.md §6.1]. |
| **Feature scope** | **Full Kaitai parity is the goal, phased.** Phase 1 = fixed structs **+ variable-length arrays + enums**. Phase 2 = conditionals, computed instances, substreams. Phase 3 = bit-sized fields, the expression sublanguage, imports. | Delivers the user's `animal_record` and most real formats early; defers the expression engine. |

---

## 3. Prior Art — what to borrow

| System | Borrow | Notes |
|---|---|---|
| **Kaitai Struct** | The whole conceptual model: declarative layout, lazy "instances", `repeat` modes (`expr`/`until`/`eos`), `enum`, `size`/`size-eos`, substreams, `meta/endian`, the navigable parsed-object tree. | Our schema is the Lambda-native embedding of `.ksy`; we keep the semantics, drop the YAML + codegen. |
| **Erlang bit syntax** | Pattern-match destructuring of binaries (`<<Ver:4, Len:16/big, Body:Len/binary>>`). | Phase 3 / integrates with [Lambda_Type_Binary.md §5.5] bit-pattern `match`. |
| **Python `construct`** | Symmetric parse/build from one declaration; `this.` references to earlier fields for lengths. | Confirms the round-trip-from-one-schema design (§6). |
| **Rust `deku`/`binread`** | Annotations on struct fields (`#[deku(bytes=4, endian="big")]`). | Direct model for our annotation syntax. |
| **FlatBuffers / Cap'n Proto** | Zero-copy access through accessors over the wire buffer. | Justifies the lazy `Velmt` over eager decode. |
| **ASN.1 / BER-TLV** | Length-prefixed / tag-length-value framing as first-class `repeat`/`size` patterns. | Variable-length is Phase 1, not an afterthought. |

**Where we beat Kaitai:** no separate language or compiler; schemas are Lambda values you can generate and parameterize; the parsed tree is a real `Element` usable by every existing Lambda facility; serialization is first-class (Kaitai's is partial/experimental).

---

## 4. Phase 1 — Schema in the Type System

### 4.1 The user's example, annotated

```lambda
binary type AnimalRecord {
    @endian: big                         // root-level default, inherited by all fields

    uuid:       uint8[16]                // 128-bit UUID, 16 raw bytes
    name:       uint8[24]                // fixed 24-byte field (NUL-padded)
    birth_year: uint16                   // inherits big-endian
    weight:     float64                  // IEEE-754, big-endian
    rating:     int32                    // signed, big-endian
}
```

- `binary type` is **not new grammar** — it is a normal `type` declaration plus the recognized binary-layout annotations. (Bare `type` also works; `binary` is a readability marker that additionally tells the validator/tools "this is a physical layout".) *Open question A below.*
- Field types are Lambda's existing fixed-width numeric types (`uint8`, `int32`, `float64`, …) — already needed by [Lambda_Type_Binary.md §5.3].
- `T[N]` = fixed-length array of `N` `T`. `uint8[16]` decodes to a zero-copy `binary` sub-view by default (raw bytes); other element types decode to a typed array view.
- `@endian: big` at the top of the body is the **root default**. Nested `binary type`s referenced as fields inherit the enclosing schema's endianness unless they declare their own.

### 4.2 Annotation vocabulary (Phase 1)

| Annotation | Meaning | Kaitai analogue |
|---|---|---|
| `@endian: big \| little \| native` | Root default endianness for the schema. | `meta: endian:` |
| `field: T/be` · `T/le` | Per-field endianness override. | per-attr `be`/`le` |
| `field: T[N]` | Fixed-length array (`N` literal or const). | `repeat-expr` w/ const |
| `field: T[count_of: other_field]` | Length taken from a previously decoded field. | `repeat-expr: this.n` |
| `field: T[until_eos]` | Repeat until end of (sub)stream. | `repeat: eos` |
| `field: binary[size: n]` | Raw byte blob of length `n` (field ref or literal). | `size:` |
| `field: binary[size_eos]` | Rest of the stream. | `size-eos: true` |
| `field: enum E` over `uintN` | Decode integer, map to a Lambda enum/symbol. | `enum:` |
| `@magic: b'\x89504E47'` | Assert constant bytes at this position; error on mismatch. | `contents:` |
| `@align: n` | Pad/skip to `n`-byte boundary before this field. | (padding) |
| `field: Sub` (another `binary type`) | Nested struct, decoded as a child `Velmt`. | user-type attr |

Variable-length & enums are Phase 1 because almost no real format is fixed-only.

```lambda
binary type Chunk {
    @endian: big
    length: uint32
    ctype:  uint8[4]
    data:   binary[size: count_of(length)]   // length-prefixed blob, zero-copy view
    crc:    uint32
}

binary type Png {
    @endian: big
    @magic:  b'\x89504E470D0A1A0A'
    chunks:  Chunk[until_eos]
}

enum Compression : uint8 { none = 0, zlib = 1, lz4 = 2 }

binary type Header {
    @endian: little
    version: uint16
    codec:   enum Compression
}
```

### 4.3 Phase 2 / 3 (sketch, out of Phase-1 scope)

- **Phase 2:** `if: <field-expr>` conditional fields; computed/derived fields (`instance`-style, lazily evaluated); explicit substreams (`process:`/`size`-bounded sub-parsers); `repeat[until: <expr>]`.
- **Phase 3:** bit-sized fields (`flags: bits[3]`), the full Kaitai expression sublanguage for lengths/conditions, schema `import`, and integration with Erlang-style bit-pattern `match` from [Lambda_Type_Binary.md §5.5].

The annotation set is forward-compatible: Phases 2–3 only add annotations, never reshape Phase 1.

### 4.4 Schema representation at runtime

A `binary type` lowers to a new `TypeBinarySchema` (sibling of the existing `TypeObject`/map-shape descriptors):

```c
// lambda/lambda-data.hpp
typedef struct BinFieldDesc {
    StrView      name;
    TypeId       elem_type;     // LMD_TYPE_UINT8 ... LMD_TYPE_FLOAT64, or nested
    BinarySchema* nested;       // non-null for sub-struct fields
    uint8_t      endian;        // resolved at schema-build time (inherited or override)
    uint8_t      repeat_mode;   // NONE | COUNT_CONST | COUNT_FIELD | UNTIL_EOS
    uint32_t     count_const;   // for COUNT_CONST
    int16_t      count_field;   // index of the field holding the length
    int32_t      fixed_offset;  // -1 unless @offset/@align makes it static
    Type*        logical_type;  // the Lambda type seen by scripts
} BinFieldDesc;

typedef struct BinarySchema {
    TypeId       type_id;       // LMD_TYPE_BINARY_SCHEMA
    StrView      name;
    uint8_t      root_endian;
    uint16_t     field_count;
    BinFieldDesc fields[];      // flexible array
    // a field is fixed-stride iff every field has a static size → enables O(1) offsetof
} BinarySchema;
```

- Built in `build_ast.cpp` when a `binary type` declaration is recognized; endianness inheritance resolved here so the decoder never re-resolves.
- A schema with all-fixed fields gets a precomputed offset table → constant-time field access on the `Velmt`.

---

## 5. Phase 1 — `Velmt`: the virtual element

### 5.1 Concept

`Velmt` is to `Element` what `VMap` is to `Map`: a vtable-dispatched virtual node that *presents* the standard element API while *storing* nothing but a buffer, a schema, and an offset. `type(velmt)` returns `"element"` — fully transparent to scripts.

```c
// lambda/lambda.hpp — mirrors the existing VMap pattern (lambda.hpp:389)
struct VElmtVtable {
    Item    (*get_attr)(void* d, Item name);     // velmt.field  / velmt[name]
    int64_t (*child_count)(void* d);             // len(velmt)
    Item    (*child_at)(void* d, int64_t i);     // velmt[i] / iteration
    Item    (*tag)(void* d);                      // element tag = schema name
    SymbolKeyList* (*attr_keys)(void* d);        // field names in declared order
    void    (*destroy)(void* d);
};

struct VElmt : Container {                        // LMD_TYPE_ELEMENT, IS_VIRTUAL flag
    Binary*       buffer;     // GC keep-alive root (owner of the bytes)
    BinarySchema* schema;
    uint32_t      base_offset;
    uint32_t      byte_len;   // resolved span of this node within buffer
    Item*         cache;      // lazily memoized decoded field Items (nullable slots)
};
```

- **Lazy:** `cache` starts empty. `get_attr("weight")` computes the field's offset (O(1) for fixed-stride schemas; a single forward walk otherwise, memoized), reads/byte-swaps the scalar, boxes it, and stores it in `cache`.
- **Zero-copy:** a `binary[...]` or `uint8[N]` field returns a sub-binary view (pointer + offset + len into `buffer`) — no copy. A nested `Sub` field returns a child `VElmt` sharing `buffer`. This is exactly the sub-binary slicing of [Lambda_Type_Binary.md §6.1]; the parent `Binary` is the GC keep-alive root.
- **API transparency:** existing element code paths (`it2elmt`, attribute access, child iteration, `for x in elmt`, `match`, formatters) work unchanged because dispatch goes through the vtable, like `VMap`.

### 5.2 The decode entry point

```lambda
let rec  = binary.decode(bytes, AnimalRecord)     // -> VElmt, O(1), nothing parsed yet
rec.name                                          // decodes 24 bytes on first access
rec.birth_year                                    // reads 2 bytes @ offset 40, byte-swapped
rec.weight                                        // f64 @ 42
type(rec)                                          // "element"  (transparent)

let png = binary.decode(file_bytes, Png)
for ch in png.chunks {                            // chunks: Chunk[until_eos]
    print(ch.ctype, len(ch.data))                 // ch is a child VElmt; ch.data zero-copy
}
```

- `binary.decode(b, Schema)` validates `@magic`/`@align` constraints eagerly (cheap, catches wrong-format inputs immediately) but defers all field decoding.
- Errors (truncated buffer, magic mismatch, enum value out of range, length field overrunning the buffer) return Lambda `error` consistent with the rest of the language.
- Variable-length structure (`until_eos`, `count_of`) forces a structural walk to know `child_count`; per the resolved decision this is **lazy fields over an eagerly-walked structure** for variable schemas, fully lazy for fixed schemas.

### 5.3 Why a new `Velmt` (not reuse `Element`)

A materialized `Element` would force eager full decode and copies — fatal for mmap'd/multi-GB inputs and wasteful when a script touches two fields of a thousand-field record. `Velmt` keeps the buffer authoritative and decodes on demand, while remaining API-identical to `Element`. This is precisely the justification FlatBuffers/Cap'n Proto give for accessor-over-buffer designs.

---

## 6. Phase 1 — Schema-driven serialization (the round trip)

One schema, both directions — the `construct`/`deku` symmetry.

```lambda
binary.encode(velmt)                 -> binary           // schema carried by the VElmt
binary.encode(value, AnimalRecord)   -> binary | error   // any element/map conforming to schema

// round-trip identity (property test target)
binary.encode(binary.decode(b, S), S) == b               // for canonical inputs
```

### 6.1 Semantics

- **Fast path — unmodified `Velmt`:** if the node is a `Velmt` over schema `S` and no field cache slot was written through a mutation API, `encode` is a single `memcpy` of the underlying view (it is already the exact bytes).
- **General path:** walk `S.fields` in declared order; for each field pull the value from the element (`get_attr`), encode with the field's resolved endianness/width, honoring `@align`/`@magic` (emit the constant), and length-prefix/variable arrays per `repeat_mode`. Output is assembled via `binary.builder()` from [Lambda_Type_Binary.md §5.1].
- **Source flexibility:** the input need not be a `Velmt` — any Lambda `Element`/`Map`/`Object` whose fields satisfy `S` (a plain constructed map of the right shape) serializes. This is what makes the type *useful for producing* binary, not just parsing it.
- **Consistency checks:** if a length field (`count_of`) disagrees with the actual array length, `encode` returns `error` rather than emitting a corrupt buffer. (Phase 2 adds an `@derive` annotation to *auto-compute* such length fields on encode.)

### 6.2 Mutation & re-encode

Phase 1 treats `Velmt` as read-only; producing modified binary = build a new conforming element and `binary.encode(elem, S)`. In-place mutable `Velmt` (write-through to a `binary.builder`) is deferred to Phase 2, paired with the builder/mutable-binary work.

---

## 7. Touch List

| File | Change |
|---|---|
| `lambda/tree-sitter-lambda/grammar.js` | Recognize `binary type` (or annotation form — see Open Q. A); field annotations `T/be`, `T[N]`, `T[count_of: f]`, `T[until_eos]`, `binary[size: …]`, `enum E`, `@endian`/`@magic`/`@align`. Then `make generate-grammar`. |
| `lambda/lambda-data.hpp` | Add `BinarySchema`, `BinFieldDesc`, `LMD_TYPE_BINARY_SCHEMA`. |
| `lambda/lambda.hpp` / `lambda.h` | Add `VElmt`, `VElmtVtable`; reuse `LMD_TYPE_ELEMENT` with an `IS_VIRTUAL` container flag (parallel to `VMap`). |
| `lambda/build_ast.cpp` | Lower `binary type` decls to `BinarySchema`; resolve inherited endianness; precompute fixed-stride offset tables. |
| `lambda/lambda-eval.cpp` | `binary.decode` / `binary.encode` sysfuncs; `VElmt` vtable impls; route element ops through vtable for virtual elements. |
| `lambda/transpile.cpp` / `transpile-mir.cpp` | Box/unbox `VElmt` through element paths; schema-literal const-pool registration. |
| `lambda/format/` | Verify formatters (JSON/YAML/Mark) traverse `VElmt` via the element API (they should, via vtable). |
| `lambda/validator/` | `binary type` schemas double as validators: "does this buffer/element conform to `S`?" |
| Depends on [Lambda_Type_Binary.md] | `Binary` byte representation (Tier 1) and sub-binary zero-copy slicing (Tier 3) — the latter is required for zero-copy `Velmt`; without it Phase 1 falls back to copying slices. |
| Tests | `test/lambda/*.ls` round-trip: `animal_record` fixed struct; PNG chunk walk (`until_eos`); length-prefixed blob (`count_of`); enum decode; `@magic` mismatch → error; truncation → error; `encode(decode(b))==b`. Add matching `*.txt` expected outputs. |
| Docs | New `doc/Binary_Schema_Support.md`; update [Lambda_Type.md](../doc/Lambda_Type.md), [Lambda_Sys_Func.md](../doc/Lambda_Sys_Func.md), [Lambda_Cheatsheet.md](../doc/Lambda_Cheatsheet.md). |

---

## 8. Phased Rollout

| Phase | Theme | Ship-gating items |
|---|---|---|
| **0 (prereq)** | Bytes | [Lambda_Type_Binary.md] Tier 1 (`Binary` repr) + Tier 3.1 (sub-binary slicing). |
| **1** | Schema + Velmt + round trip | `binary type` w/ fixed fields, fixed & variable arrays (`count_of`, `until_eos`), enums, `@magic`/`@endian`/`@align`; `BinarySchema`; lazy zero-copy `VElmt`; `binary.decode`/`binary.encode`; validator hook; full test suite. |
| **2** | Kaitai depth | `if` conditionals; computed/derived instances; substreams; `@derive` auto-length on encode; mutable/write-through `VElmt`. |
| **3** | Full parity | Bit-sized fields; the expression sublanguage; schema `import`; Erlang-style bit-pattern `match` integration; cross-runtime buffer bridge (share `Velmt` buffer with JS `Uint8Array`). |

---

## 9. Open Questions / Suggestions for You

1. **`binary type` keyword vs. pure annotation.** I propose a `binary type Name { … }` marker so tools/validator can distinguish a *physical layout* from a *logical type*, while still being "a `type` with annotations" structurally. Acceptable, or do you want plain `type` with the layout inferred purely from annotations (no new keyword at all)?
2. **`@`-annotations vs. type-position annotations.** I mixed two styles: field *type* annotations (`uint16/be`, `T[N]`) and *struct-level* `@endian/@magic/@align`. Should per-field directives (offset, magic, align) also be `@`-prefixed for consistency, or kept in type position?
3. **`Velmt` element shape — attributes vs. children.** The user said "type/struct into element, with mapped fields." I'm modeling fields as **attributes** of one element tagged with the schema name (so `rec.weight` works and `name`/order is preserved). Arrays become **child lists**. Confirm this is the intended element shape, vs. fields-as-ordered-children.
4. **Endianness scope for nested schemas.** A nested `Sub` referenced from a `big`-endian parent: inherit parent's `big` *unless* `Sub` declares its own `@endian` (my current rule), or always require `Sub` to be self-describing? The inherit rule matches Kaitai; flagging in case you want stricter locality.
5. **Failure model for partial reads.** On a truncated buffer, return `error` at `decode` time (eager structural validation) or only when the missing field is *accessed* (fully lazy)? I lean eager-structure / lazy-values for variable schemas, fully lazy for fixed — consistent with the resolved Velmt decision, but it has a UX tradeoff worth your call.

---

*End of proposal.*

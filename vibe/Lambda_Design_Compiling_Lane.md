# Lambda Compiler — Explicit Expression Representations

- **Status:** REVISED PROPOSAL. The shared representation infrastructure already exists;
  propagation through Lambda expression lowering has not started.
- **Date:** 2026-08-05
- **Scope:** how MIR Direct (`lambda/runtime/transpile-mir.cpp`) records and converts the
  representation of an emitted value. This proposal does not choose the native carrier for each
  Lambda type; those choices belong to [`Lambda_Semantics_Int_Type.md`](Lambda_Semantics_Int_Type.md)
  §5 and [`Lambda_Design_Compiling_Nullable.md`](Lambda_Design_Compiling_Nullable.md).
- **Related:** [`Lambda_Impl_Int_Issues.md`](Lambda_Impl_Int_Issues.md),
  `lambda/runtime/value_rep.h`, `lambda/runtime/mir_emitter_shared.hpp`.
- **Sequencing:** land and verify the in-flight nullable-lane work before starting this
  refactor. Both efforts touch expression representation, typed boundaries, calls, returns, and
  `transpile-mir.cpp`; they must not be developed concurrently.

---

## 1. Governing invariant

> **The compiler must carry semantic and representation decisions forward explicitly. It must
> never recover either one by interpreting emitted MIR register classes.**

There are four distinct facts with four distinct authorities:

| Fact | Authority |
|---|---|
| full semantic contract, including unions and nullability | AST and type analysis (`Type*`) |
| canonical/planned representation for an occurrence or boundary | lowering analysis (`FnValueAnalysis`, parameter/return analysis, and the consuming context) |
| representation actually emitted, plus ownership/provenance | `MirValue` |
| physical register class used to form a MIR instruction | `MirValue.mir_type`, or MIR itself inside narrowly physical helpers |

The AST is the source of truth for the semantic contract. It is **not** the sole authority for
the actual carrier: a lowering decision can box a value at one occurrence and keep the same
semantic value native at another. Once that decision is made, the emitter must record it on the
result and consumers must use the recorded representation.

`MIR_reg_type()` remains valid for genuinely physical questions such as selecting a MIR move or
forming a typed root-slot store. It is invalid as a proxy for `ValueRep`, Lambda `TypeId`,
nullability, finiteness, or pointer ownership.

---

## 2. What is wrong today

### 2.1 Expression lowering erases the emitted representation

```cpp
static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node);
```

A bare `MIR_reg_t` is only a register handle. It does not say whether the register contains a
boxed `Item`, an int lane, a full-width integer, a machine index, a float lane, or a raw pointer.
Consumers therefore reconstruct the missing fact in two ways:

1. Re-run `get_effective_type(mt, node)` and assume a representation from the result.
2. Query `MIR_reg_type()` and treat the physical register class as the representation.

The first route is redundant when it occurs after emission and is not always semantic reasoning.
`get_effective_type()` currently returns `ANY` for a match expression specifically because
`transpile_match()` boxes its result. It also unwraps nullable lane contracts to a base `TypeId`.
The function therefore mixes semantic type, effective carrier, and historical lowering
exceptions. Propagating only its `TypeId` would preserve that conflation rather than remove it.

The second route is unsound. A snapshot of the current file on 2026-08-05 contains:

| Pattern | Count |
|---|---:|
| `transpile_expr(mt, ...)` calls | 155 |
| `get_effective_type(mt, ...)` calls | 144 |
| `MIR_reg_type(...)` calls | 16 |

Twelve of the sixteen register-type queries currently answer a semantic or representation
question. Four occur in GC-root bookkeeping and answer physical storage questions. Counts and
line numbers drift; the migration must track symbols and lint rules rather than fixed source
lines.

The goal is not to delete all 144 type queries. Queries that choose lowering before emission are
legitimate. The target is to delete post-emission re-derivation and to separate the remaining
semantic-contract queries from carrier selection.

### 2.2 Why the proxy worked under v4 and failed under v5

Under v4 the relevant mapping happened to be nearly injective:

```text
MIR_T_D    <=> scalar lane (int and float shared it)
MIR_T_I64  <=> boxed Item (mostly)
```

v5 moved `int` to an i64 native lane, so these meanings now share `MIR_T_I64`:

| Meaning | C carrier | MIR register type |
|---|---|---|
| boxed `Item` | `uint64_t` | `MIR_T_I64` |
| int lane: int53 value or a lane sentinel | `int64_t` | `MIR_T_I64` |
| Lambda `int64` value | `int64_t` | `MIR_T_I64` |
| machine quantity: index, length, count, offset | signed or unsigned integer | `MIR_T_I64` |
| float lane | `double` | `MIR_T_D` |

The physical type cannot recover the semantic distinction. The failure is silent: the MIR is
usually well-typed even when its bits are interpreted under the wrong contract.

### 2.3 The resulting bug class

Each v5 incident had the same structural cause: a producer emitted representation A, a consumer
assumed representation B, and no typed boundary connected them.

| Symptom | Actual confusion |
|---|---|
| `1 + 2` became `inf` | 32-bit compare used for a 64-bit band check |
| `ndim(a)` produced garbage | int64 Item treated as a packed int lane |
| typed-array write later read as `inf` | double written into i64 lane storage |
| `-1` became `-5e-324` | int lane reinterpreted as double rather than converted |
| MIR rejected `dmov` | int lane fed to a float-lane consumer |
| shaped-field split caused 93 failures | one writer migrated while four other producers did not |

The distributed nature is the design problem: fixing one crossing does not make the next one
safe.

---

## 3. The canonical infrastructure already exists

### 3.1 `ValueRep`

`lambda/runtime/value_rep.h` defines the shared vocabulary:

```c
typedef enum ValueRep {
    VALUE_REP_NONE = 0,
    VALUE_REP_ITEM,
    VALUE_REP_I64,
    VALUE_REP_U64,
    VALUE_REP_F64,
    VALUE_REP_RAW_GC_POINTER,
    VALUE_REP_RAW_NON_GC_POINTER,
} ValueRep;
```

### 3.2 `MirValue` and the shared conversion funnel

`lambda/runtime/mir_emitter_shared.hpp` already defines the value descriptor that expression
lowering needs:

```cpp
struct MirValue {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    TypeId semantic_type;
    ValueRep rep;
    JitValueClass value_class;
    int gc_home_id;
    int scalar_home_id;
    ScalarPayloadProvenance scalar_provenance;
};
```

The same header provides `em_require_rep()`, which returns identity when the representation
already matches, delegates a real transition to the language emitter, and fails closed with
`log_error()` plus `abort()` when the transition is unavailable.

Lambda already installs `lambda_convert_rep()` and LambdaJS installs `jm_convert_rep()`.
`FnValueAnalysis` also has an `actual_rep` field, while parameter, binding, and return analyses
carry their canonical representations.

Therefore this proposal must not add a parallel `Emitted {reg, rep, tid}` abstraction or a second
`emit_as()` funnel. The work is to complete and propagate the existing abstraction.

### 3.3 The remaining gaps

1. `transpile_expr()` still returns only `MIR_reg_t`.
2. `lambda_convert_rep()` currently covers mainly Item/native boxing boundaries, not the complete
   set of representation-preserving transitions needed by expression lowering.
3. `VALUE_REP_I64` conflates Lambda `int64`, the v5 int lane, and internal machine quantities.
4. `MirValue.semantic_type` is only a `TypeId`. That is insufficient for `int` versus `int?`,
   named shapes, constrained types, or heterogeneous unions.
5. Several helpers combine representation movement with semantic coercion or use-specific error
   policy, making them unsuitable for a universal representation matrix.

---

## 4. Revised design

### 4.1 `MirValue` is the one emitted-value type

Lambda expression lowering returns `MirValue`:

```cpp
static MirValue transpile_expr_value(MirTranspiler* mt, AstNode* node);
```

Every expression producer must state the representation it actually emitted. It must not derive
`actual_rep` from `MIR_reg_type()` after the fact, and it must not blindly use the canonical
representation for the node's base `TypeId`: matches, checked calls, nullable lanes, dynamic
boundaries, and contextual boxing can legitimately differ.

`MirValue` is extended with the full semantic contract:

```cpp
struct MirValue {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    TypeId semantic_type;       // cached base dispatch key
    Type* semantic_contract;    // full contract; null only for non-Lambda/internal values
    ValueRep rep;
    JitValueClass value_class;
    int gc_home_id;
    int scalar_home_id;
    ScalarPayloadProvenance scalar_provenance;
};
```

The AST/type arena outlives MIR emission, so expression results may retain the `Type*`. Function
parameter/return/value analysis must likewise retain or recover the full contract whenever a
native nullable or constrained boundary depends on it. `LaneStorageDesc` remains the derived
storage/lane view of that contract rather than a replacement for it.

Guest and internal values that genuinely have no Lambda contract may set `semantic_contract` to
null, but their conversion hooks must then reject transitions that need one.

This is host-compiler metadata. It does not change the generated Lambda ABI or add fields to a
runtime value. Its effect on compilation throughput should be measured only if it becomes
observable; no platform register-passing claim is part of this design.

### 4.2 Split the ambiguous representations

Extend `ValueRep` with explicit logical carriers:

```c
VALUE_REP_INT_LANE,      // Lambda int/int? lane, including admitted lane sentinels
VALUE_REP_MACHINE_I64,   // compiler-internal signed quantity, never an implicit Lambda value
VALUE_REP_MACHINE_U64,   // compiler-internal unsigned quantity, never an implicit Lambda value
```

Keep the existing meanings:

```text
VALUE_REP_I64  = native Lambda int64 value
VALUE_REP_U64  = native Lambda uint64 value
VALUE_REP_F64  = native binary64 carrier
VALUE_REP_ITEM = boxed Lambda Item
```

`lambda_value_rep()` becomes a full-contract operation conceptually named
`lambda_canonical_rep(Type*)`. It returns `VALUE_REP_INT_LANE` for `int` and `int?`, with the
full contract deciding whether `INT_LANE_NULL` is admissible. It never returns a machine
representation for a Lambda semantic type.

Machine width, exact-range proofs, and use-specific invalid-value policy do not belong in a
growing `ValueRep` enum. They stay in explicit machine helpers and, when needed, a separate
machine-domain/proof descriptor. An exact-u32 analysis is an optimization proof, not a canonical
representation.

### 4.3 Keep representation conversion separate from semantic coercion

`em_require_rep(em, value, required)` means:

> Preserve the semantic value and its full contract while changing only its carrier.

Lambda's `lambda_convert_rep()` implements that relation. The relation is keyed by
`(source rep, target rep, semantic contract)`, not merely by a square pair of representations.
Examples:

| Transition | Representation rule |
|---|---|
| `ITEM -> INT_LANE` | valid only for an `int`/`int?` contract; dispatch on the actual Item tag |
| `INT_LANE -> ITEM` | pack finite values; map poison to IEEE Items; map nullable sentinel to `ItemNull` only when the contract admits null |
| semantic `int`: `INT_LANE -> F64` | exact finite conversion; map int poison to its IEEE value; preserve semantic type as `int` |
| semantic `int`: `F64 -> INT_LANE` | allowed only for a carrier already known to represent an int; never a general float-to-int cast |
| `ITEM <-> I64/U64` | use the exact full-width boxing/unboxing rule selected by the contract |
| `ITEM <-> RAW_GC_POINTER` | apply the contract's tag/null rule and preserve GC ownership metadata |
| `ITEM <-> RAW_NON_GC_POINTER` | only for an explicitly non-GC pointer contract |
| any machine transition | unavailable through `em_require_rep()` except identity |

Unsupported pairs fail closed in every build. They must not rely on a debug-only assertion.

Existing leaf helpers such as int-lane boxing, tag-dispatched unboxing, lane-to-double conversion,
and pointer boxing remain the implementation primitives. `lambda_convert_rep()` is the single
router; it does not duplicate or open-code those primitives.

The following remain **semantic coercions**, outside `em_require_rep()`:

- dynamic `Item` to a declared `Type*` through `emit_checked_boundary()`;
- Lambda value to a machine index through a checked/use-specific index helper;
- machine length/count/offset to a Lambda value through an explicitly typed constructor;
- float-to-int narrowing when the source semantic type is actually `float`;
- range, integrality, nullability, and shaped-contract validation.

This separation fixes the invalid assumption that every finite machine integer is an int-lane
value. For example, machine `INT64_MAX` is a finite quantity, while the same bits in an int lane
mean `INT_LANE_INF`. No identity conversion may cross that boundary.

### 4.4 Representation transitions preserve provenance

When a transition emits a new register, it must produce a complete `MirValue`:

- set the new `mir_type`, `rep`, and `value_class` explicitly;
- retain the full semantic contract;
- retain, transfer, or clear `gc_home_id` according to pointer ownership;
- retain or rebind scalar-home provenance according to the existing emitter rules;
- never manufacture a raw GC pointer without the precise `RootFrame`/home relationship required
  by the current GC design.

This is another reason not to introduce a smaller parallel `Emitted` type: representation and
ownership cannot safely drift into separate APIs.

### 4.5 `MIR_reg_type()` policy

Add a lint rule with this semantic policy:

> Expression lowering may not use `MIR_reg_type()` to infer `ValueRep`, semantic type,
> nullability, or pointer ownership.

Direct calls are allowed only inside named, allowlisted physical-layer helpers, initially the
root-slot bookkeeping that must form correctly typed MIR memory operations. New expression code
uses `MirValue.mir_type`; new rooting/ownership code uses `MirValue.value_class` and provenance
where available.

The four current GC-root uses are a transitional physical exception, not a claim that MIR is the
best long-term source. Once every root candidate is a `MirValue`, `value_class` can distinguish a
boxed Item or raw GC pointer from an int lane or machine scalar more precisely than
`MIR_T_I64` can.

---

## 5. Refactoring plan

Every phase keeps runtime behavior and emitted MIR unchanged. If a latent representation bug is
found, fix it in a separate commit with its own focused regression and intentional MIR-budget
update; do not hide a behavior fix inside the mechanical migration.

### Phase L0 — reconcile the existing infrastructure

1. Add `semantic_contract` to `MirValue` and its construction helpers.
2. Thread the full contract through Lambda return, parameter, call-result, and value analysis
   where native representation depends on more than `TypeId`.
3. Document the authority table from §1 next to `MirValue`/`em_require_rep()`.
4. Audit `FnValueAnalysis.actual_rep`: either make it the planned occurrence-representation
   source used by lowering or remove the unused field. Do not create a second analysis ledger.

**Gate:** build succeeds; focused emitter/ABI tests and MIR ratchets are unchanged.

### Phase L1 — split `ValueRep` without changing emission

1. Add `VALUE_REP_INT_LANE`, `VALUE_REP_MACHINE_I64`, and
   `VALUE_REP_MACHINE_U64`.
2. Update `em_mir_type_for_rep()` and `em_value_class_for_rep()`.
3. Change Lambda's canonical full-contract mapping to distinguish int lane, int64/u64, and raw
   pointers.
4. Do not automatically change LambdaJS numbers or the Python/Ruby guest policies; their public
   value models are separate.

**Gate:** zero test churn and zero MIR-emission churn.

### Phase L2 — complete the representation-preserving conversion router

1. Expand `lambda_convert_rep()` using the existing boxing/unboxing leaf helpers.
2. Keep semantic/machine coercions outside the router.
3. Add direct transition fixtures for every supported edge and fail-closed tests for unsupported
   edges.
4. Verify provenance fields after Item, pointer, and scalar-home transitions.

**Gate:** focused transition tests pass; existing MIR fixtures and budgets are unchanged.

### Phase L3 — introduce an explicit legacy boundary

Add the canonical value-returning entry and give the raw-register shim an intentionally noisy
name:

```cpp
static MirValue transpile_expr_value(MirTranspiler* mt, AstNode* node);

static MIR_reg_t transpile_expr_reg_legacy(MirTranspiler* mt, AstNode* node) {
    return transpile_expr_value(mt, node).reg;
}
```

Where an old producer cannot yet return `MirValue`, wrap it through a single temporary
`legacy_expr_value()` adapter that records the current explicit node contract. The adapter may
use existing AST/lowering analysis, but never `MIR_reg_type()`. Its call count is a migration
debt and must only decrease.

Add ratchets for:

```text
transpile_expr_reg_legacy calls
legacy_expr_value calls
semantic MIR_reg_type calls
paired post-emission get_effective_type calls
```

The compatibility shim means the C++ compiler alone does **not** enumerate unfinished work;
these ratchets do.

**Gate:** zero runtime and MIR churn; legacy counts recorded as explicit ceilings.

### Phase L4 — migrate Lambda expression clusters

Convert from producer leaves toward boundaries, one reviewable cluster at a time:

1. literals, identifiers, arithmetic, and comparison;
2. indexed loads/stores and typed arrays;
3. declarations, assignments, bindings, and phi/join results;
4. calls, returns, checked boundaries, and closures;
5. containers, fields, match/if/control flow, and statement-valued expressions.

For each cluster:

- every producer returns a complete `MirValue`;
- every consumer requests a representation through `em_require_rep()` or names an explicit
  semantic coercion;
- the legacy and post-emission-rederivation ceilings decrease;
- MIR emission remains unchanged.

### Phase L5 — remove the legacy representation inference

1. Delete `transpile_expr_reg_legacy()` and `legacy_expr_value()`.
2. Rename `transpile_expr_value()` to `transpile_expr()`.
3. Split or rename `get_effective_type()` so semantic-contract analysis no longer encodes facts
   such as "match happens to be boxed."
4. Delete semantic `MIR_reg_type()` reads.
5. Enable the final lint with only named physical-helper exceptions.

**Gate:** all legacy counters are zero; lint passes; emitted-MIR ratchets are unchanged.

### Phase L6 — audit guest emitters, do not bulk-convert them

LambdaJS already uses `MirValue`, `em_require_rep()`, and `jm_convert_rep()`. Its follow-up is an
audit of remaining raw-register expression APIs and semantic `MIR_reg_type()` inference, followed
by the same representation-preserving discipline where appropriate.

Python and Ruby have separate register/value models and language semantics. Audit them for the
same bug class, but adopt this mechanism only when it fits their existing emitter architecture.
Do not infer that a Lambda int-lane representation should become a guest numeric policy. The
frozen legacy C2MIR path is out of scope.

---

## 6. Verification

### 6.1 Direct representation tests

Exercise supported transitions with values that distinguish the representations:

- packed int53 minimum, maximum, zero, and negative one;
- `INT_LANE_INF`, `INT_LANE_NEG_INF`, and `INT_LANE_NAN`;
- `INT_LANE_NULL` under both `int?` and rejecting plain-`int` contracts;
- an int64-tagged Item presented through a semantic `int` result such as `ndim()`;
- full-width `INT64_MIN`, `INT64_MAX`, `UINT64_MAX` Items;
- float finite values, IEEE poison, and the nullable float marker;
- null and non-null raw GC pointers with their exact semantic tags;
- unsupported machine crossings, which must fail closed.

The tests must verify both the resulting value and the resulting `MirValue` metadata. Emission
fixtures are appropriate for instruction shape; executable probes are required for tag dispatch,
sentinels, and boundary behavior.

### 6.2 Per-cluster gates

For every migration cluster:

1. focused unit/integration tests for the touched representation edges;
2. existing MIR-emission fixtures and ratchet budgets unchanged;
3. `make test-lambda-baseline` green;
4. `make test262-baseline` green when shared emitter, ABI, or LambdaJS-facing code changes.

Run the final complete gates after the legacy shim and semantic register-type reads are removed.

---

## 7. Cost and risk

- **Merge conflicts:** the migration touches most expression consumers in a large, actively
  changing file. Sequence it after nullable-lane work and avoid parallel compiler refactors.
- **Host compile-time cost:** `MirValue` is larger than a raw register and may be returned through
  a host ABI memory convention on some platforms. This affects transpilation throughput, not the
  generated program ABI. Measure before introducing a smaller carrier; if one is necessary,
  factor a shared core out of `MirValue` rather than duplicate semantic metadata.
- **Metadata correctness:** a wrong `rep` becomes a local, inspectable compiler defect, but it is
  still possible. Producer-side construction funnels, fail-closed conversions, and direct
  transition tests are required.
- **Latent bugs:** the last portion will expose sites that currently work only because two
  representations share bits or a MIR class. Land those behavior fixes separately.
- **MIR ratchet limits:** instruction-count budgets catch emission churn, not every wrong operand
  meaning. Runtime edge probes remain necessary.

---

## 8. Rejected alternatives

**Add a separate `Emitted {reg, rep, tid}` type.** Rejected because `MirValue` already carries
the same core facts plus MIR type, GC classification, scalar homes, and provenance. Two emitted
value types would make representation and ownership drift independently.

**Keep returning `MIR_reg_t` and rely on call-site discipline.** This is the status quo and does
not make a representation mismatch a local failure.

**Use wrapper types only (`LaneReg`, `BoxedReg`).** Keep them as leaf-helper guards where useful,
but a wrapper per representation pair does not cover calls, nullable contracts, raw pointers,
or provenance.

**Put every conversion into one square matrix keyed only by `ValueRep`.** Representation legality
also depends on the full semantic contract. Machine index conversion, typed admission, and
float-to-int narrowing are semantic coercions with distinct error policies, not carrier-only
edges.

**Make MIR carry the distinction with `MIR_T_P`.** The distinction is not a machine property,
and the available MIR register classes do not encode it. Patching vendored MIR would violate the
vendor boundary without solving semantic nullability or machine-domain questions.

**Annotate the AST with the actual carrier.** The AST owns semantic facts. Actual representation
is contextual lowering state and belongs on `FnValueAnalysis`/`MirValue`, not as a single mutable
property of a source node.

**Treat all guest emitters as Lambda lane clients.** JavaScript, Python, and Ruby have different
numeric and public-value policies. Share emitter infrastructure where it already fits, but audit
and migrate each guest on its own semantic authority.

---

## 9. Decisions and deferred work

- **D1 — proof facts:** finite, non-null, range-checked, and exact-width facts are deferred to a
  separate analysis lattice. The full `Type*` contract is not deferred; it is required for this
  migration to preserve nullable and constrained semantics.
- **D2 — machine domains:** use distinct signed/unsigned machine representations and explicit
  semantic crossing helpers. Width/range facts remain separate proofs; the existing exact-u32
  special case does not define the general contract.
- **D3 — boxing helpers:** helpers that produce or consume semantic expression values migrate to
  `MirValue`. Raw `MIR_reg_t` remains only below the representation boundary in instruction and
  leaf conversion helpers; there is no permanent semantic raw-register shim.
- **D4 — GC-root queries:** the current physical `MIR_reg_type()` uses may remain during the
  migration. Once root candidates carry `MirValue`, prefer `value_class` and provenance and keep
  only calls that are demonstrably required to form physical MIR operands.

---

## 10. Map field storage lanes and the TypedItem slot (added 2026-08-20)

Expression representation (§1–§7) covers values in REGISTERS. This section is
the same discipline for values in PACKED MAP SLOTS: one classification
authority decides each field's storage lane, and every writer and reader obeys
it. Rulings **TB1/TB2** below were given 2026-08-20; the boundary/adoption
consequences live in [`Lambda_Design_Type_Boundary.md`](Lambda_Design_Type_Boundary.md),
which defers to this section for the storage rules. Investigation record:
`Lambda_Impl_Tune19.md` §11.

### 10.1 The classification authority and the slot formats

`type_field_storage_type_id(entry->type)` (`lambda-data.hpp`) is the single
authority; `shape_entry_storage_type_id` is its ShapeEntry wrapper. Verified
slot formats as implemented:

| classification | slot | writer arm |
|---|---|---|
| `BOOL` | 1B bool | `set_field_value` |
| `INT` | 8B i64 lane (sentinels in-band) | same |
| `INT64`/`UINT64` | 8B raw | same |
| `FLOAT` | 8B double | same |
| `NUM_SIZED` | 8B packed Item | same |
| nullable native (`int?`, `float?`, `bool?`, pointer bases) | lane w/ sentinel via `map_shape_field_store_native_lane` | checked before the switch |
| strings/symbols/decimal/dtime/complex/path + all containers | 8B raw pointer, null = 0, pointee self-describes | same |
| `LMD_TYPE_NULL` (compiler could not resolve the field) | **8B raw tagged Item** | same |
| `LMD_TYPE_UNDEFINED` | no payload (tag-only) | same |
| `LMD_TYPE_ANY` | **9B packed `TypedItem`** (`#pragma pack(1)`: 1B tag + 8B payload union) | same, plus `map_field_store` (runtime twin) |

### 10.2 When is TypedItem used on a store? — the complete inventory

A field stores as TypedItem iff its entry classifies `LMD_TYPE_ANY`, which
happens in exactly three ways:

1. **The entry's type is `any`** — declared `any` fields, and literal fields
   whose value the compiler could not type (ANY-census). This includes
   `{value: v}` where `v` is an untyped parameter — i.e. the splay
   `create_node` literal stores its `value` field as TypedItem TODAY, in the
   literal's own inferred shape.
2. **Abstract numeric contracts** — historically `TYPE_INTEGER`/`TYPE_NUMBER`
   (deliberate: a numeric Item, never a `Type*` payload). After TB4 only
   `number` remains here; `integer` reclassifies to the `Decimal*` lane.
3. **`LMD_TYPE_TYPE` non-simple contracts** — unions without a
   native-base null form, constrained `T where …`, and occurrence contracts
   `T[]`/`T[]?` (the TB1 gap).

Write sites: `set_field_value` (core; reached from `map_fill`/`set_fields`/
`set_fields_items` at construction) and `map_field_store` (runtime twin in
`lambda-eval.cpp`; reached from `map_rebuild_for_type_change`). Read sites:
`_map_read_field`'s ANY arm → `typeditem_to_item`, wrapped by
`map_shape_field_to_item` and the validator's field readers. ⚠ The C16
subtlety: an `int` in a TypedItem stores its VALUE in `double_val`, not an
Item payload.

**TB3 (2026-08-20) — why TypedItem exists, and what it does NOT license.**
TypedItem's purpose is that WIDE SCALARS pack inline: `int64`, `uint64`, and a
BigInt's `Decimal*` all fit the 9-byte tag+payload slot with **no heap box and
no extra allocation** — the same rationale as arrays packing wide scalars
inline in their payload rather than boxing per element. `{a: any}` and
`{value: v}` with `v` untyped legitimately store as TypedItem. But TypedItem
is **one possible packing, chosen by the VALUE's own shape** — a contract
never dictates packing. `{a: true}` packs `a` as a 1-byte bool slot and must
work where `{a: any}` is expected: readers resolve through the value's own
`ShapeEntry` (`map_shape_field_to_item` walks the value's TypeMap, never the
contract's), and the emitter must not assume ANY packing for an any/untyped
field — no fast lane, generic accessor only. Both halves are already enforced:
`is_direct_access_type(LMD_TYPE_ANY)` is false, and every reader dispatches on
the value's entry. TB3 makes them normative.

Three asymmetries worth knowing:

- **Two dynamic conventions coexist**: a `NULL`-classified field stores an
  8-byte raw Item; an `ANY`-classified field stores a 9-byte TypedItem. The
  9-byte form exists because full-width `int64`/`uint64` cannot live in an
  8-byte Item without a heap home; TypedItem gives them an inline payload.
- **The writer is duplicated**: `set_field_value` (core) and
  `map_field_store` (runtime) implement the same convention independently.
- **The mutation path already drifts to actual-member storage**: `fn_map_set`
  on an ANY/union-classified entry can never take its same-type fast path
  (a value's runtime type is never ANY), so it falls to
  `map_rebuild_for_type_change`, which replaces the entry's type with the
  concrete stored member. TypedItem slots are therefore written at
  CONSTRUCTION and admission-rebuild time; ordinary mutation erases them.
  This is TB2's storage model already in force on one of the two paths.

### 10.3 Rulings TB1 and TB2 (2026-08-20)

**TB1 — occurrence contracts are pointer lanes.** `T[]` and `T[]?` classify to
the pointer lane (8B `Container*`, null = 0, pointee self-describes), never to
ANY/TypedItem. The `ShapeEntry.type` keeps the full `int[]` contract — TB1
changes only the storage classification. Today's classifier misses this
because an occurrence's own `type_id` is `LMD_TYPE_TYPE`, so it falls into the
non-simple → ANY branch before the pointer-lane check can see the base.

**TB2 — union fields are packed by their ACTUAL member, and compiled code
never addresses them by fixed offset.** The union contract lives in the
admission layer only; the runtime shape records the current member (which
§10.2's mutation-path note shows is already the behavior on store); switching
members is a shape transition. Compiled code reads such fields through the
generic accessor (`map_get`/`fn_member`) — already enforced at the emitter,
since `is_direct_access_type(LMD_TYPE_ANY)` is false. Width-compatibility is
NOT sufficient to share a slot without the tag: `int | float` are both 8B but
carry i64-lane vs double — the tag is what disambiguates.

**TB4 (2026-08-20) — abstract numerics split.** `number` → TypedItem is ruled
acceptable (its domain spans every numeric carrier; the tag disambiguates).
`integer` → ruled to the **`Decimal*` pointer lane** (integer ≤ decimal), with
one consequence flagged for confirmation before implementation. The runtime
facts: there is NO `LMD_TYPE_INTEGER` value tag. `TYPE_INTEGER` is an abstract
`LMD_TYPE_TYPE` singleton; membership (`is`, validator, admission) is
`item_type_is_integer_subtype` — compact `int` (int53 lane), `int64`,
`uint64`, integral `NUM_SIZED`, **plus BigInt, which is carried as `Decimal*`
with `Decimal.unlimited == DECIMAL_BIGINT`** (`lambda.h:1357`);
`TYPE_INTEGER_VALUE = {.type_id = LMD_TYPE_DECIMAL}` is what `is`/`type()`
report for a BigInt payload. So `Decimal*` is today the carrier of the
UNBOUNDED tail only (`123n` literals): a `n: integer` field on the `Decimal*`
lane converts a compact-int store into a heap Decimal.

**CONFIRMED 2026-08-20: `integer` → `Decimal*` lane, boxing accepted** —
uniform lane, consistent with the `decimal` type. Consequence worth naming: an
`integer`-contracted field becomes a POINTER lane, so contracts containing
`integer` fields are storage-valid and adoptable under the Type_Boundary gate.

Recorded refinements, not blockers: (a) Decimal/BigInt sharing one
`LMD_TYPE_DECIMAL` tag discriminated by `Decimal.unlimited` is accepted for
now; (b) `Decimal` is currently a two-word wrapper
`{uint8_t unlimited; mpd_t* dec_val;}` (`lambda-data.hpp:160`) — the
discriminator should eventually move inside the `mpd_t` allocation so the
wrapper shrinks, using libmpdec's public fields only (vendored code is off
limits, CLAUDE.md rule 16).

**TB5 (2026-08-20) — non-simple contracts never classify ANY.** Each falls to
an existing lane: occurrence `T[]`/`T[]?` → pointer lane (TB1); constrained
`T where …` → the BASE type's lane; union → admission-only, storage records
the actual member (TB2). **Final TypedItem whitelist (confirmed 2026-08-20): exactly `any`, untyped
(ANY-census), and `number`.** Nothing else classifies to TypedItem; declared
composite contracts stop producing TypedItem slots entirely.

### 10.4 Union-typed LOCALS — the register side (verified 2026-08-20)

The register-world analogue of TB2. For a local `let a: T1 | T2 = …` there is
no shape entry to record the actual member — the boxed **Item's tag** plays
that role, and there is no TypedItem in registers (wide scalars go through
number/scalar homes instead). Verified behavior matrix:

| declaration | carrier today | verdict |
|---|---|---|
| `T \| null` (one concrete payload) | normalized to `T?` → T's NATIVE nullable lane (in-band sentinel; `x = null` and `x is null` verified correct) | ✓ correct |
| `int \| string`, dynamic RHS | boxed Item + `lambda_type_check` per unproven store | ✓ correct |
| any union, statically invalid member (`z = "oops"` into `int \| float`) | compile-time `E201` rejection | ✓ correct |
| any union, statically proven member (`z = z + 1` all-int) | check elided (T-A1 redundancy) | ✓ correct |
| **union, member SWITCH** | boxed Item carrier from declaration; both directions verified, static and dynamic | ✓ **fixed 2026-08-20** |
| union, out-of-union store (`a = 1.5` into `int \| string`) | runtime E201 at the assignment boundary, containable with `^` | ✓ **fixed 2026-08-20** (was: skipped the check, then miscompiled) |

**G6 — FIXED 2026-08-20** (was pre-existing; reproduced on the session-start
control). Root cause: the declaration lowering already decided
`var_tid = LMD_TYPE_ANY` for union annotations — with a comment stating the
invariant — and the inference-narrowing line three lines below (written for
UNANNOTATED bindings) clobbered it back to the initializer's member. Symptoms:
`z = 1.5` (static) failed MIR verification (`dmov` into the i64 register);
`z = f()` returning 1.5 dynamically **silently coerced to 1** in the old int
carrier; `a = "s"` happened to work (pointer RHS is already an i64 word),
which is why the defect hid — only a float member switch tripped it.

The fix, scoped to `TYPE_KIND_BINARY` so occurrence (`int[]`) and optional
declarations keep their existing carriers exactly:

1. the narrowing line is guarded off for binary-union annotations, and the
   initializer is boxed into the Item carrier at declaration;
2. the assignment boundary gains a membership clause: a union contract checks
   every store the compiler cannot statically prove
   (`mir_boundary_is_redundant` still elides proven members) — previously a
   concrete-but-unproven RHS (`a = 1.5` into `int \| string`) skipped the
   check entirely because its val_tid was neither ANY nor NULL.

The assignment store needed no change — its `ANY-var ← concrete val` arm
already boxes. Regression test:
`test/lambda/proc/union_local_carrier.ls` (static switches both directions,
dynamic switch, pointer member, `is` on the current member, `T \| null` lane
preservation, and the out-of-union rejection contained with `^`). Baseline
3828/3828. The benchmark corpus contains zero union-typed locals (grepped), so
there is no perf exposure and no paired run was warranted.

⚠ Probe authoring note: `x is null ++ "\n"` parses as `x is (null ++ "\n")`
and prints `false` — a precedence trap that briefly looked like a soundness
bug. Parenthesize or bind to a local before printing.

### 10.4b Known violations (gap ledger)

| gap | site | state |
|---|---|---|
| G1 | classifier sends `T[]`/`T[]?` to ANY | **FIXED 2026-08-20** — occurrence contracts classify to the pointer lane (`LMD_TYPE_ARRAY`); the pointee's own `type_id` discriminates Array vs ArrayNum, as `map?` fields already do |
| G2 | unions classify ANY at construction | open — admission-only under TB2; construction should record the actual member like mutation already does |
| G2b | constrained `T where …` classifies ANY | **FIXED 2026-08-20** — recurses to the base type's lane (self-reference guarded) |
| G5 | `integer` classifies ANY/TypedItem | ruled `Decimal*` (TB4) but **NOT implemented — needs conversion plumbing first.** `set_field_value`'s DECIMAL arm is a bare `*(Decimal**)field_ptr = item.get_decimal()`: it assumes the Item ALREADY carries a Decimal. Reclassifying without a compact-int → heap-Decimal conversion at the store (and in `map_field_store`, its runtime twin) would silently corrupt `n: integer = 3`. Split out of the G1/G2b slice for that reason |
| G6 | union-typed LOCAL's carrier narrowed from the initializer (§10.4) | **FIXED 2026-08-20** — boxed Item from declaration for binary-union annotations + membership check on unproven stores; test `test/lambda/proc/union_local_carrier.ls` |
| G3 | AST contract shapes: offsets computed under a flat `sizeof(void*)` stride, slots consumed as 9B TypedItem — malformed | **FIXED 2026-08-20** — the map-TYPE parser (`parse_type_pattern.cpp`, both sites) now strides by `type_info[type_field_storage_type_id(...)].byte_size`. It was ONE builder, not the multi-path audit §11.3 anticipated (Tune19 §12.7) |
| G4 | `set_field_value`/`map_field_store` honor ANY on G3 shapes (the byte-24 tag clobber, byte-32 overflow of Tune19 §11.3) | closes with G1–G3 |

⚠ Open observation: the `map_get ANY type is UNKNOWN: 0` [ERR!] lines visible
in the PASSING `proc_type_numeric_structural_admission` run mean an ANY slot
with tag 0 is read somewhere in today's green tree — the seam may already be
touched benignly. Audit alongside G3.

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

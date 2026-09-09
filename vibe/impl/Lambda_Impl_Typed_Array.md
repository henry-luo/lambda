# Proper `T[]` Typed Arrays for Core Lambda — Implementation Plan

- **Date:** 2026-09-09
- **Status:** PROPOSED — audit complete; implementation not started.
- **Scope:** Core Lambda `T[]` contracts from parsing through AST typing,
  admission, storage, MIR Direct, mutation, views, reflection, and system
  functions. Existing `ArrayNum` kernels are retained and brought under the
  contract model.
- **Out of scope:** LambdaJS `TypedArray`, new image algorithms, manual SIMD,
  dependent-shape types, a C-text/C2MIR back end, and changes to vendored MIR.
- **Prior records:** `../Lambda_Typed_Array.md`,
  `../Lambda_Typed_Array2.md`, `../Lambda_Typed_Array4.md`,
  `../Lambda_Design_Type_Enforcement.md`,
  `../Lambda_Design_Runtime_COW.md`,
  `../Lambda_Design_Compiling_Lane.md`, and
  `../Lambda_Issue_Ledger.md` (`LR12-4`, `LR12-5`).
- **Formal authority:** **S7.7.2–S7.7.6** (checked boundaries and failure
  routing), **S7.8.1** (container admission comes from the destination
  contract), **S7.10.5–S7.10.6** (typed vector results and mutator errors),
  **S9.1.1–S9.1.3**, **S9.1.5v2** (finality, COW, `var` sharing, and the
  no-reference-cell value model), **S9.2.1–S9.2.2**
  (value covariance, borrow invariance, and view confinement), **S11.1.1**
  (`T[]` is the homogeneous-array spelling), **S11.3.1v2** (structural array
  membership), **S11.4.1–S11.4.5** (annotations are enforced contracts),
  **S12.2.1** (exact embedding and annotated reassignment), **D2.4.1–D2.4.3**
  (semantic contract and representation are separate facts), **D2.5.1–D2.5.3**
  (nullable lanes and `T?` reads), **D2.6.1–D2.6.5** (array representations,
  ArrayNum/nullability, and append semantics), **D2.6.11** (the pinned
  container header), **D2.8.1–D2.8.3** (error-free
  lane entry), **D3.1.1v2–D3.1.3** (the full `Type*` graph is authoritative),
  **D3.2.3–D3.2.4v2** (declared/effective separation and map reification),
  **D3.3.3–D3.3.4** (binding-local narrowing and full-contract
  representation), **D3.4.5–D3.4.6** (transactional writes and shared lane
  descriptors), and **D5.3.3–D5.3.4** (precise roots across allocating
  boundaries).

---

## 1. Outcome

After this plan, `T[]` is one coherent contract across every Lambda surface:

1. `T[]` denotes an actual homogeneous array whose immediate elements satisfy
   `T`. It is not a scalar occurrence that happens to admit one value
   (**S11.1.1**).
2. Nesting is compositional: `T[][]` is an array of `T[]`; a two-dimensional
   packed `ArrayNum` is a representation of `T[][]`, never a license to treat
   a row as `T` (**S11.1.1**, **D2.6.2**).
3. A declaration, parameter, return, indexed write, `push`, and array-producing
   system function all use the same full `Type*` contract (**S7.8.1**,
   **S11.4.1**).
4. An unproven scalar indexed read from `T[]` has public type `T?`; a proven
   in-bounds read may use `T` privately in the emitter (**D2.5.3**,
   **D3.3.4**).
5. Value positions are covariant when every element embeds exactly. A `var`
   parameter is invariant and requires the same structural array contract
   (**S9.2.1**).
6. Physical storage is unobservable. A boxed `Array`, native-lane `Array`, and
   `ArrayNum` compare, iterate, format, match, and type-check identically when
   they represent the same value (**D2.6.2**).
7. A wrong element never enters a declared array. Failure is static where
   provable and otherwise a rich, transactional runtime error; it is never a
   lossy conversion, silent no-op, `null`, unchanged-success result, or wrong
   value (**S11.4.1**, **S12.2.1**, **S7.10.6**).
8. A typed array pays an O(n) admission at most when an uncertified value first
   crosses the contract. The resulting representation certificate makes
   repeated same/subtype value boundaries O(1). No element check is emitted at
   every read or every already-admitted call (**S7.7.2**, **D3.2.4v2**).
9. Record arrays retain enough physical proof for `xs[i].field` to combine a
   bounds/null guard with the named record's direct byte-offset access. A
   `MAP` tag alone never proves the record layout (**D3.1.1v2**,
   **D3.2.4v2**).
10. Read views are snapshot values. A write-through view is a non-escaping
    `var` borrow created only for a mutable place and checked for exclusivity
    (**S9.1.1–S9.1.3**, **S9.2.2**).

Representative required behavior:

```lambda
type Variable = {value: int, name: string}
type Constraint = {strength: int}

let variables: Variable[] = [
  {value: 1, name: "x"},
  {value: 2, name: "y"}
]

// public type Variable?; an in-bounds loop proof may use Variable privately
let maybe_variable = variables[0]

pn append_variable(var values: Variable[], value: Variable) {
  push(values, value)
}

// must reject before mutation; Constraint and Variable are both MAP-tagged,
// but they do not carry the same named layout.
// push(variables, {strength: 10})
```

Typed empty construction is representation-directed:

| Contract | Required empty carrier |
|---|---|
| unannotated `[]` | open boxed `Array`, no certificate |
| `int[]` | empty `ArrayNum(ELEM_INT)` certified as `int[]` |
| `u8[]` | empty `ArrayNum(ELEM_UINT8)` certified as `u8[]` |
| `int?[]` | empty native-lane `Array` certified as `int?[]` |
| `string[]` | empty non-null pointer-lane `Array` certified as `string[]` |
| `Variable[]` | empty concrete-layout pointer lane (`MapVariable*[]`) certified as `Variable[]` |
| `(A | B)[]` or `any[]` | boxed `Array` certified with the full union/open contract |

---

## 2. Audit baseline and root cause

### 2.1 Current split

The implementation currently has four partial notions of an array type:

| Layer | Current fact | Failure mode |
|---|---|---|
| Declared syntax | `T[]` is `TypeUnary(OPERATOR_REPEAT, T)` | indistinguishable from general occurrence semantics |
| Literal inference | `TypeArray::nested` | separate shape from declared `T[]`; inference compares mostly `TypeId` |
| Runtime carrier | `Array` lane bits or `ArrayNum::elem_type` | records `MAP`, not `Variable`; no full contract or rank proof |
| MIR | `MirVarEntry::full_type` plus `elem_type: TypeId` | fast paths use the lossy witness; missing metadata can default to `int` |

AST, MIR, and runtime separately implement helpers for extracting an array's
element type. Their accepted wrappers already differ. This is a design smell,
not merely missing optimization: **D3.1.1v2** says the complete `Type*` graph
is the semantic authority and a TypeId alone is never a contract.

### 2.2 Reproduced failures on 2026-09-09

The audit used the current `lambda.exe` with `LAMBDA_TIER=jit` and
`LAMBDA_TIER=interp`. Temporary probes were removed after the run.

| Probe | JIT result | T0 result | Broken invariant |
|---|---|---|---|
| append unrelated named record to `A[]` | admitted | admitted | map tag substituted for named shape |
| append integer to literal-backed `A[]` | appended | rejected silently | literal boundary skipped; mutator has no contract |
| append integer to literal-backed `string[]` | appended | rejected silently | tier divergence and silent mutation failure |
| append to `int[]` | unsupported | unsupported | `ArrayNum` has no growable typed append (`LR12-4`) |
| pass `B[]` to `var A[]` | not statically rejected; runtime error does not route like T0 | fails | `var` invariance compares operand TypeIds |
| admit dynamic `[300]` to `u8[]` | rejects | wraps to `[44]` | T0's sized conversion is lossy |
| checked write to `u8[]` | corrupt fallback behavior | corrupt fallback behavior | compact byte lane takes an eight-byte `ELEM_INT` store |
| `let m:int[]=[[1,2],[3,4]]; m[0]` | `1`, type `int` | `[1,2]`, type `array` | rank erased; JIT reinterprets a row as a scalar |
| N-D string write into `int[]` | stores `nan` | stores `nan` | raw N-D setter is coercive and contract-free |
| mask write on declared `int[]` | rejected as non-integer key | same | checked scalar path intercepts the supported mask path |
| mutate `transpose(let_base)` | mutates the `let` base | same | a stored view acts as an escaping mutable alias |

### 2.3 Root cause

`ArrayNum` is a capable physical numeric container, but it is being used as if
its element-width enum were the language contract. Native pointer arrays do
the same with a base `TypeId`. Neither fact proves a composite element type,
null/error admission, named layout, union membership, or immediate array rank.

The repair therefore cannot be another `if (elem_type == ...)` fast path. It
must separate four authorities exactly as **D2.4.1** requires:

1. **semantic contract:** canonical `Type*` (`Variable[]`, `int?[][]`);
2. **planned representation:** boxed/native/ArrayNum plus lane and rank;
3. **runtime representation proof:** what this value was constructed or
   admitted to carry;
4. **MIR value representation:** register/pointer form and proof provenance.

---

## 3. Semantic model to implement

### 3.1 Give `T[]` an array-only unary operator

Keep `Type.kind == TYPE_KIND_UNARY`, but stop representing bracket-array syntax
with the general occurrence operator. Add a distinct operator such as
`OPERATOR_ARRAY`:

- `T[]` parses/builds as `TypeUnary(OPERATOR_ARRAY, T)`;
- `T*`, `T+`, and count/range patterns retain occurrence semantics;
- `[T]` remains the exact one-element structural bracket pattern;
- a scalar `v is T[]` is false even when `v is T`;
- arrays, lists admitted as arrays, ranges admitted as numeric sequences, and
  `ArrayNum` may satisfy `T[]` according to the normal structural rules.

This is an implementation correction to **S11.1.1**, not a new syntax. If the
CST does not expose enough information to distinguish the spelling, update
`lambda/tree-sitter-lambda/grammar.js` and regenerate with
`make generate-grammar`; never edit generated `parser.c`.

### 3.2 Canonical homogeneous contracts

`TypeUnary(OPERATOR_ARRAY, operand)` becomes the canonical semantic form for
homogeneous arrays in declarations, inferred expression types, parameters,
returns, and system-function results. `TypeArray` remains for exact positional
bracket patterns/tuples and their per-slot pattern data.

Introduce one shared resolver in `lambda/runtime/type_contract.*`:

```c
typedef struct LambdaArrayContractInfo {
    Type* contract;
    Type* immediate_element;
    Type* leaf_element;
    LaneStorageDesc leaf_lane;
    uint8_t rank;
    uint8_t is_homogeneous;
    uint8_t accepts_null;
    uint8_t accepts_error;
} LambdaArrayContractInfo;

bool lambda_array_contract_info(Type* type, LambdaArrayContractInfo* out);
```

The exact names may follow existing conventions, but there must be one
implementation. It replaces `ast_declared_array_element`,
`mir_array_occurrence_element`, `runtime_array_contract_element`, and local
`known_array_element_type` variants.

Rules:

- `immediate_element(int[][]) == int[]`;
- `leaf_element(int[][]) == int` and `rank == 2`;
- `any[]` is a real declared array contract, not the open `array` type;
- an exact tuple `[int, string]` is not a homogeneous array contract;
- missing/ill-formed metadata fails closed; it never defaults to `int`;
- nullable/error information is read recursively from the full operand.

### 3.3 Structural relations

Add shared array relations used by the AST checker, runtime admission, MIR
boundary elision, and validator:

```c
typedef enum LambdaArrayContractRelation {
    ARRAY_CONTRACT_INCOMPATIBLE = 0,
    ARRAY_CONTRACT_DEFERRED,
    ARRAY_CONTRACT_VALUE_SUBTYPE,
    ARRAY_CONTRACT_EXACT,
} LambdaArrayContractRelation;
```

- Value `S[] -> T[]` is covariant when every `S` value embeds exactly into
  `T`; value-aware numeric cases defer to admission (**S9.2.1**,
  **S11.4.5**).
- `var S[] -> var T[]` requires structural exactness, including rank,
  nullability, union structure, sized numeric kind, and named-map layout
  identity (**S9.1.3**, **S9.2.1**).
- Named record arrays use the same trusted map relation/reification rules as
  scalar named maps (**D3.2.4v2**).
- `Type*` pointer equality is an optimization after canonicalization, never
  the definition of array-type equality.

### 3.4 Rank and physical N-D storage

The semantic rank is the number of nested `OPERATOR_ARRAY` nodes. Physical
N-D `ArrayNum` remains a flat data buffer plus shape/stride metadata.

| Expression/contract | Public semantic result |
|---|---|
| `m: int[][]; m[i]` | `int[]?` |
| `m: int[][]; m[i,j]` | `int?` |
| `m: int[][]; m[mask]` | `int[]` unless the operation explicitly preserves rank |
| `flatten(m)` / `ravel(m)` | `int[]` |
| `transpose(m)` | `int[][]` |
| `reshape(xs, [r,c])` with static 2-item shape | `T[][]` |
| `reshape(xs, dynamic_shape)` | safe open `array` public type plus a runtime representation certificate |

Dependent dimensions and rank-polymorphic user signatures are out of scope.
When rank cannot be proved statically, the compiler must choose a safe boxed
public type rather than guess a scalar lane. Runtime shape metadata may still
preserve the optimized physical representation.

---

## 4. Runtime representation certificate

### 4.1 Purpose

A binding contract and a value's representation proof are not the same fact:

- the binding owns the language-level declaration (**D3.2.3**);
- the value may carry proof that it was constructed/admitted under a layout;
- that proof may justify a fast boundary or direct load;
- the proof must never be used to infer or narrow an unrelated unannotated
  binding (**D3.3.3**).

Introduce an immutable, interned `ArrayRepCert` shared by arrays with the same
representation proof. A proposed logical payload is:

```c
typedef struct ArrayRepCert {
    Type* array_contract;       // full canonical T[] / T[][] proof
    Type* immediate_element;
    Type* leaf_element;
    LaneStorageDesc leaf_lane;
    uint8_t rank;
    uint8_t flags;              // exact/reified/error-free/etc.
} ArrayRepCert;
```

It is a representation/admission certificate, not the result of `type()` and
not an intrinsic nominal type on the array value.

### 4.2 Carrier location and ABI

Append `ArrayRepCert* rep_cert` to both `List`/`Array` and `ArrayNum`, and to
the C mirror in `lambda/lambda.h`. `Element` inherits the `List` tail and
normally leaves it null unless its content face gains a homogeneous proof.

This choice:

- keeps the pinned eight-byte `Container` header unchanged (**D2.6.11**);
- preserves every existing field offset used by MIR and GC;
- keeps `sizeof(List) == sizeof(ArrayNum)` for legal representation changes;
- costs one pointer per list/array/element rather than a hash lookup at every
  boundary/read;
- requires all exact-size assertions, C mirrors, MarkBuilder embedded payload
  offsets, heap-size accounting, clone/copy routines, and JS numeric promotion
  assumptions to be updated together.

The certificate is allocated/interned under the same type-descriptor lifetime
owner as existing `TypeMap*` layout descriptors. It is not a GC object. If a
certificate can outlive that owner, retain the owning Script/module generation
exactly as named-map values already must; do not introduce an unrooted GC edge.

### 4.3 Certificate lifecycle

| Operation | Certificate action |
|---|---|
| typed literal with statically admitted elements | install target/inferred exact certificate |
| successful dynamic `T[]` boundary | install target certificate after all elements pass/reify |
| COW clone/snapshot | copy certificate pointer |
| exact typed scalar/row/mask write | preserve certificate |
| typed `push` | preserve certificate after successful admission |
| `splice` removal | preserve certificate |
| untyped heterogeneous write/push | clear certificate when representation widens |
| `slice`/`take`/`drop` | derive same-contract certificate |
| leading-axis N-D view | derive rank-minus-one certificate |
| transpose | derive same-rank certificate |
| flatten/ravel | derive rank-one leaf certificate |
| concat/stack/zip | install the result contract computed by the shared rule |
| external/buffer view | install only after the external element width, rank, and mutability are validated |

Certificates are trusted only when installed by a reviewed construction or
admission choke point. `TypeId`, `ArrayNumElemType`, or matching byte width
alone may never synthesize one.

### 4.4 Physical representation selection

Representation follows the complete admitted leaf contract:

| Contract family | Physical representation |
|---|---|
| non-null `int`, `float`, `i64`, `u64`, sized numeric, bool | `ArrayNum` with the exact element kind |
| nullable `int?`, `float?`, bool?, nullable narrow sized integer | native-lane `Array` using the ruled sentinel/widened lane |
| nullable `i64?`/`u64?` | boxed Item-lane `Array` per **D2.5.2** |
| non-null pointer scalar such as string/symbol/binary | non-null pointer-lane `Array` |
| nullable pointer scalar | nullable pointer-lane `Array` |
| exact named map/element | concrete-layout pointer lane such as `MapVariable*[]`; every member is reified to that exact layout |
| union, `any`, error-admitting element, heterogeneous tuple | boxed Item-lane `Array` |
| nested homogeneous numeric rank | N-D `ArrayNum` when rectangular; nested boxed arrays otherwise |

No native lane is selected if the element contract admits `error` or the
producer is not statically proved error-free (**S7.8.1**, **D2.8.1–D2.8.3**).

For every lane-safe mapping from a Lambda element contract `T` to its concrete
native representation `t`, the homogeneous array representation closes over
that mapping: `T[]` uses a contiguous `t[]` lane. In particular, if the named
record `Variable` maps to `MapVariable*`, then `Variable[]` maps to
`MapVariable*[]`, **not** to the layout-erased `Map*[]`. `MapVariable` is
notation for the concrete packed layout identified by `Variable`'s canonical
`TypeMap`; an implementation need not emit that literal C typedef, but MIR's
lane proof must retain the equivalent descriptor identity and fixed field
offsets. The common `Map` prefix is usable for generic runtime operations only
after leaving the specialized lane. Treating it as the lane element type would
discard the reification proof required by **D3.2.4v2**.

---

## 5. Shared admission and mutation APIs

### 5.1 Admission

Replace semantic uses of `ensure_typed_array` and `ensure_sized_array` with one
contract-aware boundary:

```c
Item lambda_array_admit(Item source, Type* target_contract,
    const char* boundary);

bool lambda_array_rep_proves(Item value, Type* target_contract,
    bool invariant);
```

Admission algorithm:

1. Resolve and validate the full target with `lambda_array_contract_info`.
2. Reject non-sequence sources. Array/list/range handling is explicit; a
   scalar is never accepted merely because an occurrence count includes one.
3. If the source certificate structurally proves the value target and the
   physical representation is compatible, return/share under normal COW in
   O(1). For `var`, require the invariant relation.
4. Otherwise perform a validation/planning pass over every logical element.
   Check exact numeric embedding, null/error admission, nested rank/shape, and
   named-map relation. Do not modify the source.
5. Select and allocate the destination representation only after the plan is
   known. Root source, candidate, converted elements, and shape metadata across
   every allocation (**D5.3.3–D5.3.4**).
6. Reify named maps into the exact target layout; recursively admit nested
   arrays; write numeric lanes only with already-admitted values.
7. Install the target certificate after construction completes.
8. Publish the candidate only after final representation validation. On any
   failure return a rich `ItemError` with boundary, expected contract, actual
   value/type, index path, and source location (**S11.4.1**).

This makes an initial dynamic `Variable[]` boundary O(n), but every subsequent
same-contract call O(1). It also makes direct record fields legal: each map was
reified before the array certificate was installed (**D3.2.4v2**).

### 5.2 Exact element store

Split today's coercive `array_num_set_item` responsibilities:

```c
Item array_num_store_admitted(ArrayNum* array, int64_t offset,
    Item admitted_value);

bool array_num_store_untyped(ArrayNum* array, int64_t offset, Item value);
```

- The admitted function switches exhaustively over all 14 element kinds and
  writes using the exact element width. It has no catch-all `ELEM_INT` arm.
- It never converts a string to `nan`, wraps `300` into `u8`, or turns an
  arbitrary number into bool.
- The untyped function may preserve existing widening behavior for an open
  unannotated array, but it must report failure and must never be called for a
  declared typed destination.
- `ensure_sized_array`, N-D assignment, masks, views, image writes, and JS
  bridges must explicitly select the correct API; no void setter may sit on a
  checked boundary.

### 5.3 Checked mutations

All source-language mutation lowers through destination-contract-aware APIs:

```c
Item lambda_array_set_checked(Item owner, const LambdaIndexPlan* index,
    Item value, Type* array_contract, const char* boundary);

Item lambda_array_push_checked(Item owner, Item value,
    Type* array_contract, const char* boundary);

Item lambda_array_splice_checked(Item owner, Item start, Item count,
    Type* array_contract, const char* boundary);
```

The names may reuse existing entry points after migration. The required shape
is fixed:

1. derive the immediate/leaf destination contract from the declared root;
2. normalize and validate every index/mask/range before mutation;
3. admit every RHS scalar/row/vector into temporary rooted storage;
4. prepare/detach the COW candidate;
5. perform exact-width writes or growth;
6. preserve/derive the certificate;
7. publish the candidate through the caller's root only after success.

Multi-position mask/slice writes preflight all positions and RHS elements.
They do not leave a partially changed typed array when a later element fails
(**S11.4.1**, **D3.4.5**).

### 5.4 Growth and nullability

- `push` grows every owned 1-D typed representation amortized O(1), including
  `ArrayNum`. It admits the value before capacity growth and returns an error
  on rejection (**S7.10.6**, `LR12-4`).
- Pointer-lane record append reifies the incoming named record if necessary,
  then preserves the array certificate.
- A null written/appended to plain `T[]` is rejected.
- A null written/appended to `T?[]` uses the ruled native nullable lane. If the
  current carrier is non-null `ArrayNum`, build the replacement buffer first,
  then atomically retag/publish the same-size `List`/`ArrayNum` header or return
  a replacement through the `var` root (**D2.6.2**).
- `splice` removal preserves a typed contract. Views and N-D arrays may keep
  the explicit `copy()/ravel()` requirement under `LR12-5`; they must return a
  real error rather than unchanged success.
- Internal collection builders retain a clearly named unchecked append only
  when their construction proof already owns every element. Content builders
  continue to use `list_push`; array builders use verbatim collection append
  per **D2.6.5**.

---

## 6. Phased implementation

Every phase is separately landable. Do not proceed past a phase with a known
wrong result, tier divergence, memory overwrite, or missing expected output.

### Phase 0 — Pin the failures and issue inventory

**Implementation**

- Convert the audit probes into permanent focused fixtures under
  `test/lambda/` and `test/lambda/proc/`; add a corresponding `.txt` for every
  new `.ls` file.
- Add negative fixtures for wrong literal members, wrong dynamic members,
  record-shape mismatch, `var` invariance, null/error admission, lossy sized
  numeric values, rank mismatch, and view escape.
- Add JIT/T0 differential rows to `test/test_interp_gtest.cpp`.
- Add compact-width canary tests to `test/test_item_repr_gtest.cpp`: write the
  first, middle, and last element of every `ArrayNumElemType`, checking adjacent
  bytes and guard storage.
- Reproduce current `ArrayNum` equality representation sensitivity and empty
  result carrier loss.
- Synchronize live findings into the central issue ledger; do not create a
  second ledger in this file.

**Exit gate**

- Every reproduced failure is a failing or negative test with a named formal
  ruling.
- The compact setter test fails safely under `lambda-debug-asan.exe`, not by
  crashing the test runner.
- No fixture encodes silent rejection or lossy conversion as success.

### Phase 1 — Close the compact-width safety hole

**Primary files/symbols**

- `lambda/runtime/lambda-eval.cpp`: `fn_array_set`.
- `lambda/runtime/lambda-data-runtime.cpp`: `array_num_set_item`,
  `array_num_set_nd`, `ensure_sized_array`.
- `lambda/lambda.h`: public declarations.
- Every direct caller found by `rg`, including mask, image, buffer, and view
  paths.

**Implementation**

- Add exhaustive exact-width storage helpers; no default case may alias an
  unknown kind to `ELEM_INT`.
- Make every setter return success/error.
- Validate exact embedding before a compact store. Use the shared numeric
  admission foundation, not casts.
- Make sized-array conversion two-pass and transactional. `[300] -> u8[]`
  fails identically on both tiers.
- Route N-D and mask stores through the exact function when a declared
  contract is present.

**Exit gate**

- All 14 element kinds pass canary, first/middle/last, JIT, T0, and ASan tests.
- No source-language typed write calls a coercive void setter.

### Phase 2 — Canonicalize the semantic `T[]` contract

**Primary files/symbols**

- `lambda/runtime/ast-core.hpp`: unary operator enum.
- `lambda/runtime/parse_type_pattern.cpp`: bracket-array construction.
- `lambda/runtime/type_contract.hpp/.cpp`: canonical resolver and relations.
- `lambda/runtime/ast.hpp`, `build_ast.cpp`, `transpile-mir.cpp`,
  `lambda-eval.cpp`, and validator call sites: remove local extractors.
- `lambda/tree-sitter-lambda/grammar.js` only if the CST needs a distinct node;
  regenerate, never hand-edit `parser.c`.

**Implementation**

- Add `OPERATOR_ARRAY` and migrate only `T[]` to it.
- Normalize homogeneous inferred array expression types to that form.
- Reserve `TypeArray` for exact bracket patterns/tuples.
- Implement full structural exact/subtype/deferred relations.
- Remove all operand comparisons based only on `TypeId`.
- Make scalar `is T[]` false; preserve occurrence semantics for actual
  occurrence syntax.
- Make diagnostics render nested, nullable, union, sized, and named-record
  arrays without dropping structure.

**Exit gate**

- One shared contract resolver remains.
- `A[]` and `B[]` are distinguishable even when both operands are MAP-tagged.
- `int[]`, `int?[]`, `u8[]`, `(int|string)[]`, `Variable[]`, and `int[][]`
  round-trip through formatting and matching.

### Phase 3 — Add and propagate `ArrayRepCert`

**Primary files/symbols**

- `lambda/lambda.hpp`, `lambda/lambda.h`: layouts and C mirror.
- `lambda/runtime/lambda-mem.cpp`, GC layout constants/asserts, clone/COW code.
- `lambda/io/mark_builder.cpp` and embedded payload offsets.
- Array/List/Element/ArrayNum constructors in `lambda/core/lambda-data.cpp`,
  `lambda/runtime/lambda-data-runtime.cpp`, and collection builders.
- JS numeric promotion sites that require equal `List`/`ArrayNum` size; no JS
  language semantics change.

**Implementation**

- Append the certificate pointer without changing existing offsets.
- Add certificate interning and lifetime ownership.
- Initialize it to null on every open/Input/foreign construction path.
- Copy it on snapshots/clones; clear it on untyped widening; derive it on
  shape-preserving transforms and views.
- Add debug assertions that the certificate's lane/rank agrees with the
  carrier. Assertions diagnose compiler/runtime bugs; they are not user checks.
- Extend memory-size reporting and GC/Mark copies to the new total struct size.

**Exit gate**

- C and C++ layout mirrors pass static assertions.
- Forced-GC tests retain certificates and all Type*/shape lifetimes.
- `sizeof(List) == sizeof(ArrayNum) == sizeof(Element)` remains true.
- Untyped arrays have no accidental semantic narrowing.

### Phase 4 — Unify declaration, parameter, and return admission

**Primary files/symbols**

- `lambda/runtime/lambda-eval.cpp`: `runtime_type_admit_array`,
  `runtime_type_admit_value`, `lambda_type_check`.
- `lambda/runtime/interp.cpp`: `interp_coerce_declared_array` and parameter /
  return boundaries.
- `lambda/runtime/transpile-mir.cpp`: `emit_checked_boundary`, declaration,
  argument, parameter-adapter, and return lowering.
- Retire semantic use of `ensure_typed_array` / `ensure_sized_array`; keep only
  clearly internal packing helpers if still useful.

**Implementation**

- Implement `lambda_array_admit` and certificate fast proof.
- Remove the MIR `!declared_array_literal` boundary exception.
- Push an expected array contract into literal construction; statically proved
  literals may install the certificate without a redundant runtime walk.
- Make JIT and T0 call the same boundary helper for dynamic values.
- Reify every named map member before installing a `Variable[]`-like
  certificate.
- Enforce plain/value parameter covariance, `var` invariance, and declared
  return contracts.
- Route all failure as the call/declaration/return error required by
  **S7.7.2–S7.7.6**; never log and continue.

**Exit gate**

- Exact output and exit status match between JIT and T0 for the full admission
  matrix.
- A certified same-contract parameter call performs no element walk or clone.
- Wrong elements report the first logical index path and never establish the
  destination binding.

### Phase 5 — Expected-type inference, rank, and indexed result types

**Primary files/symbols**

- `lambda/runtime/build_ast.cpp`: `build_array_from_items`,
  `static_boundary_relation`, `type_exact_match`,
  `direct_field_result_type`, call and return inference.
- `lambda/runtime/type_contract.*`: LUB and array relation helpers.
- AST nodes that need literal logical length/rank independent of semantic type.

**Implementation**

- Contextually type array literals from a declaration/argument/return target.
- Check each literal element against the immediate target contract.
- Infer a normalized element LUB for uncontextualized non-empty arrays:
  identical full types remain exact; numeric types use the canonical numeric
  result/admission rules; distinct compatible records/unions build a normalized
  union rather than using the first shared TypeId.
- Leave uncontextualized `[]` open; it acquires a typed carrier only when it
  crosses a typed boundary.
- Infer rectangular nested numeric literals as nested semantic array types and
  optionally choose one N-D physical `ArrayNum`.
- Make indexed reads rank-aware and nullable: one index removes one rank;
  full-rank coordinate indexing returns the nullable leaf.
- Preserve array contracts for slices/masks where the operation's semantics
  return a collection.
- Keep mutable binding declarations intact. Mutation invalidates a physical
  proof only when it actually changes representation; it does not change the
  binding's declared result type (**D3.2.3**, **D3.3.3**).

**Exit gate**

- `m:int[][]; m[0]` is `int[]?` on both tiers.
- A mutable `xs:T[]` read is `T?`, not `any`.
- Bounds-proved loops emit a private non-null fast path without changing the
  AST's public type.

### Phase 6 — Carry full array proof through MIR Direct

**Primary files/symbols**

- `lambda/runtime/mir_emitter_shared.hpp`: `VarEntry`.
- `lambda/runtime/transpile-mir.cpp`: typed parameter analysis, index-policy
  selection, array caches, invalidation, module slots, direct calls, adapters,
  and boundary elision.
- `MirValue` and representation router helpers governed by **D2.4.1–D2.4.3**.

**Implementation**

- Replace `elem_type: TypeId` as the semantic witness with full
  `Type* array_contract`, `Type* immediate_element`, rank, representation, and
  guarded/proven provenance. A compact TypeId may remain only as derived
  physical dispatch data.
- Delete the fail-open default to `int`.
- Make bool, all sized numerics, nullable native arrays, record arrays, and
  nested arrays participate in guarded/proven policies.
- Fix boundary elision structurally: remove the accidental enclosing MAP gate
  and contract pointer-identity requirement.
- At a public/raw function adapter, admit once and install the body proof.
  Every dominated use consumes that proof without re-admission
  (**S7.7.2**).
- At a dynamic/module load, guard the certificate and physical carrier before
  taking a native/direct path; fall back to boxed admission, never reinterpret.
- For `Variable[]`, make `xs[i]` produce a nullable `Variable` representation
  proof. After the null/bounds arm, direct fields use `Variable`'s certified
  offsets without `fn_member_by_id` or map-shape recovery.
- Invalidation follows actual effects: whole-binding rebind, untyped widening,
  unknown dynamic mutation, or escaped borrow. A checked same-contract write,
  splice removal, or typed push preserves the proof.
- Typed array cache entries include certificate/rank identity, not only carrier
  tag and element TypeId.

**Exit gate**

- MIR dumps contain no `ensure_typed_array`, `lambda_type_check`, or O(n)
  admission inside an already-admitted typed loop/body.
- Repeated `int[]` and `Variable[]` calls cross in O(1).
- A bounds-proved `Variable[]` element field read lowers to the array load and
  direct packed-field load, with only required null/error guards.
- No MIR semantic decision probes `MIR_reg_type()` or guesses from TypeId.

### Phase 7 — Contract-aware writes, `push`, masks, and N-D assignment

**Primary files/symbols**

- `lambda/runtime/lambda-eval.cpp`: checked flat assignment.
- `lambda/runtime/lambda-data-runtime.cpp`: exact ArrayNum store and N-D index.
- `lambda/runtime/lambda-vector.cpp`: mask/slice assignment and view writes.
- `lambda/runtime/collection_runtime.cpp`: `array_push`, `pn_push`,
  `pn_push_cow`, `pn_splice`.
- T0 and MIR assignment/call lowering.

**Implementation**

- Build one `LambdaIndexPlan` for scalar, multi-coordinate, leading-axis,
  range/slice, and boolean-mask destinations.
- Route every declared root through the checked mutation APIs in §5.3.
- Implement exact row replacement for N-D arrays, checking rank and dimensions
  before copying.
- Implement mask scalar broadcast and array RHS assignment transactionally.
- Add amortized growth for owned 1-D `ArrayNum`; preserve element width and
  certificate.
- Make native-pointer `array_push` return an error on wrong tag and still run
  the full named-shape check before that storage operation.
- Implement nullable ArrayNum demotion and publication through `var` roots.
- Keep untyped arrays dynamically widenable, but clear any physical
  certificate and never weaken an annotated root.
- Remove silent/log-only mutation failure paths.

**Exit gate**

- The same wrong value is rejected at literal, flat write, row write, mask
  write, `push`, parameter, and return boundaries.
- All failures leave the original owner byte-for-byte and observably unchanged.
- `int[]`, `u8[]`, `bool[]`, `string[]`, `Variable[]`, nullable arrays, and
  boxed union arrays all support their valid mutation/growth cases.

### Phase 8 — Make views obey value and borrow semantics

**Primary files/symbols**

- `lambda/runtime/lambda-vector.cpp`: `fn_subview`, `fn_reshape`,
  `fn_transpose`, `fn_ravel`.
- `lambda/runtime/lambda-data-runtime.cpp`: leading-axis view construction and
  backing resolution.
- AST place/borrow analysis, `NameEntry::view_base`, T0 frames, and MIR `var`
  argument lowering.

**Implementation**

- Ordinary view-producing functions create read views with
  `is_mutable_view = 0` and a derived certificate.
- Binding a view produces a snapshot value. A later write through that local
  detaches/materializes an owned typed array; the source remains unchanged
  (**S9.1.1–S9.1.2**).
- Only a direct mutable-place argument to a `var` parameter may create a
  write-through view borrow. Construct that capability in the call adapter,
  not in the general value-producing builtin.
- Record ultimate base, range/stride, and lifetime in the borrow frame; reject
  escape, return, capture, storage in a container, or passage to a plain
  parameter.
- Apply writer/writer exclusivity to base-vs-view and view-vs-view arguments.
  The existing conservative whole-base rejection remains acceptable until a
  disjoint-range proof is implemented.
- A mutable borrow derives rank and element proof from its base certificate and
  publishes through the base/root on successful return.

**Exit gate**

- Writing a local view never changes a `let` or another `var` snapshot.
- A sanctioned `var` view argument writes through with no full-buffer copy.
- Overlapping mutable views reject before entering the callee.
- Forced-GC and external-buffer tests prove base/backing lifetime.

### Phase 9 — Type all array-producing operations and reflection

**Primary files/symbols**

- `lambda/runtime/sys_func_registry.h/.c`: argument/result type rules.
- `lambda/runtime/build_ast.cpp`: one centralized system-function type-rule
  evaluator.
- Vector/math/image runtime result constructors.
- `fn_is`, validator occurrence/array handling, equality, total order,
  iterators, readers, and formatters.

**Implementation**

- Replace ad hoc fixed-`any` rows with a finite shared rule set such as:
  preserve array contract, leaf array, mask-same-rank, static reshape,
  concat-LUB, stack-rank-plus-one, zip-tuples, shape-int-array, and numeric
  promotion. Do not add another per-function switch after the third shared
  shape; extend the registry rule table.
- Give `push`/`splice` an argument relation tying the owner contract to the
  appended value rather than `ANY, ANY -> ANY`.
- Cover at least:

| Family | Required type behavior |
|---|---|
| reverse/sort/unique/take/drop/slice/subview | preserve element contract; preserve/derive rank |
| flatten/ravel | return leaf `T[]` |
| transpose | preserve leaf and rank |
| reshape | derive static rank; dynamic rank returns safe open public type |
| concat | common structural element/rank contract |
| stack | add one semantic rank when statically known |
| zip | array of exact pair/tuple patterns |
| comparisons | bool array of the broadcast result rank |
| mask selection | flattened element array unless explicitly rank-preserving |
| shape | `int[]` |
| reductions | scalar or rank-reduced typed result according to axis |
| matmul/vector math | canonical numeric promotion plus result rank |
| image/stencil operations | exact documented numeric result carrier |

- Every zero-length result installs the same certificate as a non-empty result
  (**S7.10.5**).
- Make `is T[]`, validator admission, and match use the same structural
  relation. A certificate can make the positive check O(1); uncertified values
  are walked.
- Repair `ArrayNum`/Array structural equality and ordering so representation is
  invisible (**D2.6.2**).
- Ensure `type()`, formatting, MarkReader, and serialization expose semantic
  array behavior, never `ArrayNumElemType` or certificate identity.

**Exit gate**

- No array-producing registry row remains `TYPE_ANY` without an explicit
  semantic reason recorded beside it.
- Empty/non-empty results have the same public type and physical certificate.
- Equality and `is` give identical answers for equivalent boxed/native/
  ArrayNum representations.

### Phase 10 — Benchmark/script migration, docs, and closeout

**Implementation**

- Change growable benchmark record stores only after Phase 7:
  `Planner.vars: Variable[]`, `Planner.constraints: Constraint[]`, and the
  equivalent AWFY `World` stores. Keep identity/foreign-key handles as scalar
  ints; this plan does not introduce reference cells (**S9.1.5v2**).
- Re-audit every `*2.ls` benchmark. Replace open `array` only when one stable
  homogeneous contract is true; do not annotate genuinely heterogeneous
  collections to chase a score.
- Update `../Lambda_Typed_Array*.md` implementation-status sections so they no
  longer claim completed type enforcement or freely stored mutable views.
- Update `doc/Lambda_Procedural.md`: an incompatible write to an annotated
  typed array rejects; it never automatically converts the declared array to a
  generic one (**S12.2.1**).
- Update `doc/Lambda_Type.md`, validator docs, and examples for array-only
  `T[]`, exact `[T]`, nested rank, nullable arrays, covariance, and invariant
  `var` parameters.
- Mark this file `(done)` only after all correctness, baseline, sanitizer,
  release, and mechanism gates pass.

**Performance gates — release build only**

- Run `make release`; never measure a debug build.
- A certified `T[] -> T[]` call is O(1) and emits no element walk/clone.
- A hot typed numeric loop performs direct length/index/lane operations with no
  per-iteration boxing, type check, or generic `fn_index`.
- A hot `Variable[]` loop performs direct element load and named-field offset
  access after the required bounds/null check.
- Typed `push` is amortized O(1) for owned 1-D arrays.
- Compare `deltablue2`, `richards2`, `havlak2`, `hashmap2`, and numeric typed
  rows against the pre-change release baseline. Existing archived native
  reference results may remain comparison data, but no C-text/C2MIR product
  path is added.
- Report instruction/helper histograms from MIR dumps as mechanism evidence;
  wall-clock improvement alone does not prove the correct path landed.

**Closeout gate**

- `make test-lambda-baseline` passes 100%.
- `make test` passes, or every unrelated pre-existing failure is separately
  identified with evidence.
- JIT/T0 differential typed-array suite is exact.
- `make build-debug-asan` plus the focused typed-array suite reports no memory
  error.
- Release benchmark outputs/checksums are unchanged.

---

## 7. Required test matrix

Every cell below needs construction, read, valid write, invalid write,
parameter, return, `push` where meaningful, empty-result, COW, and JIT/T0
coverage.

| Element contract | Literal/direct | Dynamic admission | Flat | N-D | Mask | `var` | View |
|---|---:|---:|---:|---:|---:|---:|---:|
| `int`, `float` | required | required | required | required | required | required | required |
| `i64`, `u64` | required | required | required | required | required | required | required |
| `i8/i16/i32/u8/u16/u32` | required | required | required | required | required | required | required |
| `f16/f32` | required | required | required | required | required | required | required |
| `bool` | required | required | required | required | required | required | required |
| string/symbol/binary | required | required | required | n/a | selection | required | snapshot |
| named `Variable` / wrong `Constraint` | required | required | required | nested | selection | invariant | snapshot |
| nullable numeric/pointer | required | required | required | required | required | required | required |
| union and error-admitting | required | required | required | nested | required | invariant | snapshot |
| `any[]` versus open `array` | required | required | required | nested | required | invariant | snapshot |
| nested `T[][]` / `T[][][]` | required | required | row+leaf | required | required | invariant | required |
| empty arrays/results | required | required | bounds | shape | empty mask | required | required |

Cross-cutting assertions:

- rich error includes the logical index path;
- failed mutation preserves owner, aliases, certificate, length, capacity, and
  bytes;
- exact sized bounds test min/max and one-below/one-above;
- float tests cover ordinary values, `nan`, infinities, and exact/inexact
  numeric admission under the formal numeric rules;
- nullable tests cover null store, read, append, COW, and `var` publication;
- record tests use two named types with the same top-level MAP tag and
  deliberately different field layouts;
- forced GC runs during initial admission, growth, reification, COW detach,
  N-D view creation, and nullable demotion;
- all public operations are exercised with both empty and non-empty inputs.

---

## 8. File-level work inventory

| Area | Expected files | Work |
|---|---|---|
| Type model | `lambda/lambda-data.hpp`, `runtime/ast-core.hpp`, `runtime/type_contract.*` | canonical array operator/info/relation/LUB |
| Parser/type AST | `runtime/parse_type_pattern.cpp`, optionally `tree-sitter-lambda/grammar.js` | distinguish `T[]` from occurrences |
| AST builder | `runtime/build_ast.cpp`, `runtime/ast.hpp` | expected typing, rank, nullable reads, structural calls |
| Runtime layout | `lambda.hpp`, `lambda.h` | certificate pointer and ABI mirrors |
| Storage | `runtime/lambda-data-runtime.cpp`, `core/lambda-data.cpp` | exact stores, constructors, rank/cert lifecycle |
| Admission | `runtime/lambda-eval.cpp`, `runtime/interp.cpp` | one full-contract boundary and tier parity |
| MIR Direct | `runtime/mir_emitter_shared.hpp`, `runtime/transpile-mir.cpp` | full proof, adapters, direct read/write, elision |
| Mutation/COW | `runtime/collection_runtime.cpp`, COW/path helpers | typed growth, transactional writes, publication |
| Views/vector | `runtime/lambda-vector.cpp`, shape/backing helpers | snapshot values and confined mutable borrows |
| Sysfunc typing | `runtime/sys_func_registry.h/.c`, `runtime/build_ast.cpp` | centralized generic array result/argument rules |
| Validator/reflection | `validator/validate_pattern.cpp`, `fn_is`, equality/order | one relation, representation invariance |
| GC/Mark | `runtime/lambda-mem.cpp`, `runtime/gc/*`, `io/mark_builder.cpp` | new size/lifetime/copy assertions |
| Tests | `test/lambda/**`, `test/test_lambda_gtest.cpp`, `test/test_interp_gtest.cpp`, `test/test_item_repr_gtest.cpp` | full matrix and sanitizer canaries |
| Benchmarks/docs | `test/benchmark/**/*2.ls`, `vibe/Lambda_Typed_Array*.md`, user docs | typed stores, truthful status, examples |

Before adding a helper, grep for the existing semantic operation. At the third
per-kind or per-operation variant, extract a table/shared helper first; do not
grow another switch independently in AST, T0, MIR, and validator code.

---

## 9. Progress checklist

| Phase | Status | Required evidence |
|---|---|---|
| 0. Pin failures | not started | permanent regressions + issue-ledger sync |
| 1. Compact-width safety | not started | 14-kind canary + ASan |
| 2. Canonical `T[]` | not started | one resolver; structural relation tests |
| 3. Representation certificate | not started | ABI/GC/COW/lifetime tests |
| 4. Unified admission | not started | exact JIT/T0 boundary matrix; O(1) repeat |
| 5. Inference/rank/reads | not started | nested and nullable inferred-type tests |
| 6. MIR proof | not started | MIR helper/instruction gates |
| 7. Checked mutation/growth | not started | flat/N-D/mask/push transactional matrix |
| 8. Views/borrows | not started | snapshot, write-through, exclusivity, GC |
| 9. Sysfunc/reflection | not started | registry census; empty/equality invariance |
| 10. Migration/closeout | not started | baselines, full suite, release report, docs |

Update this table only with concrete evidence: test names, counts, benchmark
artifact, and commit/date. “Implemented” without a gate is not progress.

---

## 10. Risks and rejected shortcuts

### 10.1 Container-size growth

The certificate pointer grows the common array/list/element object. This is a
real memory cost and must be measured. A heap side table avoids eight bytes per
container but adds lookup, GC removal, and synchronization on every proof use;
it is rejected for the initial design because typed access and boundary
elision are hot paths. If measurement forces reconsideration, change the
representation through a formal/design review, not by removing the proof.

### 10.2 Type lifetime

A certificate points into the type graph. Reuse the named-map descriptor
lifetime model and verify cross-module exports, closures, async frames, and
retained runtime values. Never place a pool pointer in a value whose owner can
die first.

### 10.3 Certificate forgery/staleness

Do not install a certificate from an annotation alone, a TypeId, element
width, or a successful first-element check. It is installed only after exact
construction/admission of the entire logical value. Every representation-
changing untyped operation clears it; every preserving operation asserts it.

### 10.4 Rank guessing

Do not treat `ArrayNum::is_ndim` as proof that a declared `T[]` means arbitrary
rank. The semantic contract is compositional; physical rank metadata implements
it. Dynamic-rank transforms return a safe public type until Lambda gains an
explicit rank-polymorphic type design.

### 10.5 Benchmark-only typing

Do not special-case `Variable`, `Constraint`, `deltablue2`, or map arrays. The
two-record MAP-tag regression must pass because the general structural
contract and reification rules are correct.

### 10.6 Per-access validation

Walking a record array on every parameter call or field read is sound but
defeats the declared-boundary dominance required by **S7.7.2**. Admission plus
an immutable proof is the required model.

### 10.7 Silent widening

An open unannotated array may change representation. A declared `T[]` may not
silently become generic after a wrong write. That older behavior in
`doc/Lambda_Procedural.md` conflicts with **S11.4.1** and **S12.2.1** and must
be removed, not preserved as compatibility.

### 10.8 Freely stored mutable views

“Created in a `pn`” is not a sufficient borrow rule. A stored bit cannot prove
base ownership, non-escape, or exclusivity. Mutable view capability belongs to
the `var` call/place boundary under **S9.2.2**, never to an ordinary array
value.

### 10.9 Restoring a C-text backend

Native reference programs may remain benchmark oracles. They are not a Lambda
backend and no C2MIR support, CLI switch, transpiler, or ABI path is added.

---

## 11. Definition of done

Proper `T[]` support is complete only when all of the following are true:

1. One canonical full-contract resolver and one structural relation are used
   by AST, validator, runtime, T0, and MIR.
2. `T[]` is array-only, `[T]` is exact-one, and nested brackets determine
   semantic rank.
3. Every typed construction/boundary installs a trustworthy representation
   certificate; no binding is inferred from that certificate.
4. Literal, declaration, argument, `var` argument, return, flat/N-D/mask
   write, `push`, and `splice` all enforce the same destination element rule.
5. Every compact kind stores at its exact width and passes sanitizer canaries.
6. Wrong values and lossy sized conversions are rejected transactionally with
   identical rich errors on JIT and T0.
7. Unproven indexed reads are nullable and rank-correct; proved reads optimize
   privately.
8. Same-contract repeated calls are O(1), and typed hot loops do not re-admit,
   rebox, or rediscover element layout.
9. Named-record arrays retain direct packed-field access while preserving
   Lambda's value/COW semantics.
10. Typed numeric, nullable, pointer, record, union, and nested arrays support
    their valid mutations and typed empty results.
11. Ordinary views are snapshot values; only confined exclusive `var` borrows
    write through.
12. Equality, matching, iteration, formatting, and reflection are independent
    of boxed/native/ArrayNum representation.
13. The full test matrix, forced GC, ASan, Lambda baseline, full suite, and
    release mechanism/performance gates pass.
14. `*2.ls` benchmarks and public/design documentation state the actual typed
    contracts and no longer encode implementation workarounds as language
    semantics.

Until all fourteen conditions hold, the feature remains partial and this file
must retain its unsuffixed, not-complete name.

# Proper `T[]` Typed Arrays for Core Lambda — Implementation Plan

- **Date:** 2026-09-09
- **Status:** COMPLETE in worktree `temp/typed-array-proper`.
- **Scope:** Core Lambda homogeneous `T[]` contracts from parsing through
  runtime admission, storage, MIR Direct, mutation, transforms, views,
  reflection, typed benchmark scripts, tests, and user documentation.
- **Out of scope:** LambdaJS `TypedArray`, manual SIMD, dependent dimensions,
  a C-text/C2MIR backend, and edits to vendored MIR.
- **Formal authority:** **S11.1.1v2** (homogeneous arrays and rank),
  **S11.4.1v3** (checked construction and mutation), **S7.7.2** (checked
  boundaries), **S7.10.6** (mutator errors), **S9.1.1–S9.2.2** (value/COW and
  `var` borrowing), **D3.1.1v3** (the full `Type*` graph is authoritative),
  **D3.2.4v3** (named-layout reification), and **D3.3.3v3** (a certificate is
  not inferred narrowing).

## 1. Outcome

`T[]` is a homogeneous contract, not a compact element tag. `T[][]` is an
array whose immediate elements are `T[]`; `T[n]` remains an occurrence/count
pattern, and `[T]` remains an exact one-position structural pattern
(**S11.1.1v2**).

Every typed construction, declaration, parameter, return, indexed mutation,
`push`, `splice`, and typed array-result operation must use the same full
semantic contract. A dynamic source pays a complete, transactional admission
once. Its resulting proof may make later compatible boundaries and direct
reads O(1), but it cannot narrow an unrelated binding (**S11.4.1v3**,
**D3.3.3v3**).

Required examples:

```lambda
type Variable = {value: int, name: string}
type Constraint = {strength: int}

var values: Variable[] = dynamic([{value: 1, name: "x"}])
push(values, dynamic({value: 2, name: "y"}))
// push(values, dynamic({strength: 1}))  // rich error; no mutation

var grid: int[][] = dynamic([[1, 2], [3, 4]])
let row = grid[0]       // int[]?
```

## 2. Root cause and non-negotiable separation

The older implementation mixed four distinct facts:

| Fact | Authority | It must not be replaced by |
|---|---|---|
| Language contract | full `Type*` graph | `TypeId` or byte width |
| Physical carrier | `Array`, native-lane `Array`, or `ArrayNum` | a declaration alone |
| Value proof | immutable `ArrayRepCert` | inferred local element type |
| MIR register form | `ValueRep` / machine type | the source-language contract |

`ArrayNumElemType == ELEM_UINT8` proves only byte layout. It does not prove
rank, named-map offsets, null/error exclusion, union membership, or that a
particular `u8[]` contract admitted the value. Similarly, a `MAP` tag is not
the proof for `Variable`: it erases the fixed packed layout required for a
direct field read (**D3.1.1v3**, **D3.2.4v3**).

## 3. Semantic representation

### 3.1 One array operator and resolver

`T[]` builds `TypeUnary(OPERATOR_ARRAY, T)`. Counted occurrence syntax keeps
its occurrence operator. The shared resolver is the only semantic extractor:

```c
struct LambdaArrayContractInfo {
    Type* array_contract;
    Type* immediate_element;
    Type* leaf_element;
    LaneStorageDesc leaf_lane;
    uint8_t rank;
    uint8_t has_leaf_lane;
};
```

It accepts declared homogeneous arrays and inferred homogeneous `TypeArray`
values, rejects tuples/pattern arrays, preserves nested rank, and fails closed
when any metadata is absent. Structural compatibility compares array rank and
the complete element contract; pointer identity is only a cache optimization.

### 3.2 Native mapping

If a Lambda type `T` has a native carrier `t`, a reified homogeneous array
uses a contiguous `t[]` lane. This is concrete by contract:

| Lambda element contract | Carrier |
|---|---|
| `int`, `float`, `i64`, `u64`, bool, sized numeric | exact `ArrayNum` lane |
| nullable scalar/pointer | nullable native `Array` lane where supported |
| `string`, `symbol`, `binary` | typed pointer lane |
| `Variable` | `MapVariable*` (the `Variable` packed layout descriptor) |
| `Variable[]` | `MapVariable*[]` |
| union, `any`, error-admitting, tuple | boxed `Item` lane |
| nested homogeneous numeric value | nested semantic arrays; rectangular N-D storage only when its shape is proved |

`Variable[]` is never `Array<Map*>` as a semantic or direct-codegen type.
The common `Map` prefix is usable only by generic runtime operations; the
certificate retains the concrete `Variable` descriptor and its offsets
(**D3.2.4v3**).

### 3.3 Immutable representation certificate

`Array`, `List`, `Element`, and `ArrayNum` carry an optional trailing
`ArrayRepCert*`, mirrored in the C ABI after existing fields so the container
header remains pinned. A certificate is interned per runtime heap and contains
the full contract, immediate/leaf contracts, rank, leaf lane, and flags for
exact/error-free/reified admission.

It is installed only after construction or full successful admission. A proof
is usable only when the current carrier still matches its rank and exact lane:
`ArrayNum` must have the certified compact element kind; a pointer or nullable
native contract must have the certified native `Array` lane; and nested or
boxed contracts must have ordinary `Item` slots. COW preserves that relation.
A transform that materializes a new matching carrier may retain the proof;
one that stages boxed Items clears it, so the next typed boundary re-admits and
publishes the native lane. An unchecked representation-changing write also
clears it. A compatible checked write, removal splice, or typed append retains
it. Certs live in the heap pool with the `Type*` values they reference; they
are not GC objects and no global cache may retain a dead context
(**D3.3.3v3**, **S11.4.1v3**).

## 4. Admission, storage, and mutation

1. `lambda_type_check` routes homogeneous contracts to one array admission
   path.
2. That path roots candidate/source/element values across every allocating
   step, admits every logical immediate element, reifies named maps, and only
   then publishes a canonical lane and certificate.
3. Non-null scalar contracts rebuild to their exact `ArrayNum` kind; pointer
   and nullable contracts rebuild an `Array` native lane; open/union contracts
   remain boxed.
4. A failed element admission returns the normal rich boundary error and
   leaves the destination and its certificate unchanged (**S11.4.1v3**).

Compact stores use `array_num_store_admitted`: it switches over every real
`ArrayNumElemType`, verifies the exact item contract, performs a width-correct
write, and returns failure. No typed path may call a coercive void setter.
All checked indexed writes and append operations validate before committing;
untyped writes may widen an open array but may never silently weaken a declared
array (**S7.10.6**).

## 5. MIR Direct requirements

The JIT receives `Type*` array contracts at explicit boundaries. A fast path
must guard:

1. carrier kind and required plain layout;
2. non-null `ArrayRepCert` and exact/error-free flags;
3. semantic rank, leaf layout contract, lane kind, and nullability.

It then loads the native carrier directly. Its fallback is representation-safe
`item_at`/`fn_index`, never reinterpretation. A typed numeric load receives
its scalar lane; `string[]` loads a raw `String*` and boxes it correctly;
`Variable[]` loads a raw `MapVariable*` and the existing certified direct
member read consumes `Variable`'s packed field offsets. Repeated compatible
calls must not rewalk elements or rediscover map layout.

The guard compares the certificate proof fields, not only the enclosing
`TypeUnary*`: separately allocated but semantically compatible spelling must
not force a generic boundary. Named-map direct access still requires the exact
reified layout descriptor, because compatible map tags alone are unsafe.

## 6. Transform and view policy

| Operation | Certificate rule |
|---|---|
| `slice`, `take`, `drop`, `reverse`, `sort`, `unique` | preserve/copy only a proof preserved by the operation |
| flatten/ravel | derive rank-one leaf proof |
| leading-axis N-D row | derive rank-minus-one proof |
| transpose | retain rank/leaf proof only with valid shape/stride carrier |
| concat/stack/zip | install a newly calculated result contract |
| unchecked write/push | clear before representation can change |

Ordinary views are value snapshots. A write-through view remains a confined
`var` borrow made at the call/place boundary, never a freely storable mutable
array value (**S9.1.1–S9.2.2**). It inherits a proof only while its backing
range, base, and lifetime are valid.

## 7. Phased work and evidence

| Phase | Deliverable | Evidence required before complete |
|---|---|---|
| 0 | regression inventory | permanent `.ls`/`.txt` fixtures and tier differential test |
| 1 | exact compact storage | 14-kind first/middle/last canary and ASan run |
| 2 | `OPERATOR_ARRAY`/resolver | nested, nullable, named, union formatting and relation tests |
| 3 | certificate ABI/lifetime | static layout assertions, COW/GC preservation, heap-local interning |
| 4 | one admission boundary | declaration, param, return and dynamic-source matrix, transactional failure |
| 5 | rank-aware static typing | nested reads, public nullability, contextual literals and LUB tests |
| 6 | guarded MIR lanes | dump assertions: no admitted-loop walk; direct record element + direct field |
| 7 | all mutators | flat/row/mask/push/splice transactional tests, amortized typed growth |
| 8 | views/borrows | snapshot, allowed `var` write-through, overlap/escape and GC tests |
| 9 | sysfunc/reflection | typed result-rule census, empty-result proofs, equality/`is` parity |
| 10 | migration/closeout | all `*2.ls` audit, docs, baseline/sanitizer/release evidence |

The worktree implementation has landed all Phase 1–10 mechanisms: one resolver
and admission path, the physical certificate, exact compact/pointer/named-map
lanes, rank-aware T0 and MIR Direct lowering, checked flat/path/mask/N-D
mutation, COW-safe views/borrows, certificate-safe transforms, audited
benchmarks, documentation, and release/sanitizer evidence. No phase was
accepted solely because a benchmark happened to complete.

## 8. Required matrix

Every supported carrier requires direct and dynamic construction, reads,
valid/invalid write, parameters, returns, typed `push`, empty result, COW, and
MIR/T0 parity.

| Contract family | Required edge cases |
|---|---|
| `int`, `float`, `i64`, `u64`, bool | compact representation, native reads, null/out-of-bounds result |
| `i8/i16/i32/u8/u16/u32/f16/f32` | bounds, first/middle/last writes, no adjacent corruption |
| pointer scalars | raw-lane read, nullability, GC after growth |
| named map/element | unrelated same-tag record rejection, reification, direct field read |
| nullable | null construction/read/write/push without unwanted demotion |
| union/`any` | boxed representation and no false certificate |
| `T[][]`/`T[][][]` | rank-preserving indexing and row admission |
| ranges and zero-length output | materialization and target carrier/certificate |
| mask, slice, N-D | semantic rank, transactional mutation, certificate derivation |

Cross-cutting assertions: errors include logical path; failure preserves bytes,
length, capacity, aliases, and certificate; forced collection occurs during
admission/growth/COW/reification; and structurally equal boxed/native/packed
arrays compare and format the same.

## 9. Verification and closeout

Use a release build for all performance measurements (**D3.3.3v3**):

```sh
make release
./lambda.exe run test/lambda/proc/proc_proper_typed_array.ls
./test/test_interp_gtest.exe --gtest_filter=InterpWalker.ProperTypedArrayContractsKeepExactLanesAndRecordLayouts
./test/test_item_repr_gtest.exe --gtest_filter=RuntimeShapeTransition.ArrayNumExactStoresKeepEveryCompactLaneIsolated
make test-lambda-baseline
make build-debug-asan
```

The finish gate additionally requires the typed-array differential matrix,
MIR mechanism inspection, relevant view/system-function tests, all audited
typed benchmarks preserving output/checksum, documentation updates, and a
clean `git diff --check`. A benchmark score alone cannot demonstrate correct
typed arrays; the emitted guards and direct load path must prove the intended
mechanism.

## 10. Rejected shortcuts

- No map tag or element width may substitute for a `Type*` contract.
- No per-access full validation: admission plus certificate dominates reads.
- No silent typed-array widening or log-and-continue mutation failure.
- No certificate created from one element, a declaration alone, or a lane tag.
- No C-text/C2MIR fallback or vendor-MIR modification.
- No typed benchmark annotation unless the collection actually has a stable
  homogeneous contract.

## 11. Implementation record and closeout audit

The implementation follows the boundaries above rather than adding a second
`TypeId`-only typed-array path:

- `OPERATOR_ARRAY` is distinct from occurrence/count syntax and all rank,
  immediate-element, lane, and compatibility questions go through
  `lambda_array_contract_info`.
- `runtime_type_admit_array` is the single dynamic boundary. It roots the
  source and every allocating intermediate, validates/reifies the full
  element contract transactionally, then installs an interned `ArrayRepCert`.
  Open boxed `any[]` values receive their proof in place; an `ArrayNum` source
  first becomes a boxed outer sequence so N-D rows cannot flatten on an open
  write (**S11.4.1v3**, **D3.3.3v3**).
- `ArrayNum` has an exact, non-coercive store for every compact element kind.
  Raw writes clear a certificate; checked writes retain or renew it only after
  physical-carrier verification. Generic transforms which stage boxed `Item`s
  clear their proof and re-admit at the next typed boundary.
- MIR Direct guards a certificate before raw numeric, string, and named-map
  lane access; its fallback is representation-safe. Typed returns use the
  same firewall. The T0 interpreter calls the same checked runtime helpers.
- The nested named-map hot path first proves the root's existing reified
  layout and checks its scalar leaf before COW publication. Its per-heap
  relation cache is bounded and hash-probed; array-valued leaves retain the
  ordinary detached admission path so their independent certificate cannot be
  bypassed (**D3.2.4v3**, **D3.3.3v3**).

`rg --files test/benchmark -g '*2.ls'` found 69 benchmark scripts. The
closeout review promotes only the two DeltaBlue variants: their data really is
homogeneous (`Variable[]` is the concrete `MapVariable*[]` carrier and the
constraint slot is explicitly nullable). The remaining `*2.ls` declarations
that still use open `array` represent intentionally heterogeneous JSON, AST,
tuple, text-pipeline, or dynamic-node values; assigning a false homogeneous
annotation would violate **S11.1.1v2** rather than improve code generation.

Final closeout validation (2026-09-09):

- `make build-test`, the compact-lane unit tests, and the interpreter proof
  test all passed;
- new positive `proc_proper_typed_array` coverage for scalar, pointer,
  nullable, named-map, nested, dynamic/range/element, return, transform,
  concat, reflection, mask, and N-D cases;
- new negative golden tests for nested, mask, and N-D invalid writes;
- the focused typed-array/direct-map/COW script group passed 37/37;
- the MIR mechanism and ratchet suites passed 86/86 and 16/16. The reviewed
  certificate guard has a new size budget because its rank/lane/error-free
  proof and representation-safe fallback are required by **D3.3.3v3**;
- `make test-lambda-baseline` passed all Input tests (2104/2104) and all
  Lambda tests in scope (3008/3012). The four residual failures are the
  pre-existing JS/Test262-harness cases
  `IndexesFunctionParentsStructurallyInTopLevelAwaitModule`,
  `DirectEvalAnnexBUsesRetainedTest262Assertions`,
  `RunsJsonReviverWrapperThroughRetainedTest262Harness`, and
  `Test262Prelim.RunnerContracts`; they were not masked or changed by this
  array work;
- `make release` passed; the release fixture passed, AWFY `deltablue2`
  passed in 33345.5 ms, and JetStream `deltablue2` passed in 23216.7 ms; and
- the final fixture passed under AddressSanitizer with no diagnostics.

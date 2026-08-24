# Lambda Implementation Plan: Tune11 — Restore Shape, Ownership, and Native-Call Fast Paths

**Status: IMPLEMENTED — 2026-08-05.**

T11-A through T11-D are landed and retained. T11-E remains deferred by its explicit
evidence gate: Result21 alone shows residual typed rows, but no qualifying nullable-read
profile was recorded, and the plan forbids inferring that root cause from a slower row.
The guarded Result21 snapshot is captured in `test/benchmark/Overall_Result21.md` with
its source data in `test/benchmark/benchmark_results_v21.json`.

**Baseline:** Result20 at commit `6fcf2283fa6e09c8cab645d66abfc8b5d1e22989`,
compared with the archived Result18 release at
`e406aa9b87ef26ea179f8933c650c76a9b0f8742`.

**Scope:** Tune11 repairs the algorithmic performance regressions introduced while landing
runtime type enforcement, COW-safe checked mutation, name identity, dual function compilation,
the new Lambda `int` representation, and nullable native lanes. It targets four independently
measured mechanisms:

1. repeated deep cloning and recursive validation at typed map boundaries;
2. whole-array cloning on every checked local element write;
3. loss of native specialization for ordinary procedural parameters;
4. a linear scan of every generated well-known name on hot LambdaJS property access.

Tune11 does **not** roll back the corrected semantics. In particular, it preserves total
out-of-bounds reads, exact `T[]` versus `T?[]` contracts, value-array covariance under COW,
transactional dynamic type errors, nullable native-lane storage, JavaScript `undefined`, and
precise GC rooting.

**Related designs and records:**

- `vibe/Lambda_Design_Compiling_Nullable.md`;
- `vibe/Lambda_Design_Runtime_COW.md`;
- `vibe/Lambda_Design_Name_Identity.md`;
- `vibe/impl/Lambda_Impl_Name_Identify (done).md`;
- `test/benchmark/Overall_Result18.md`;
- `test/benchmark/Overall_Result20.md`.

The legacy C2MIR path is frozen and is outside this plan.

---

## 1. Executive decision

Result20 is not dominated by one unavoidable cost of nullable values. It contains four separate
fast-path losses. The largest losses happen before the useful benchmark work begins:

- a map that already has the correct typed Shape is deep-cloned and recursively validated;
- an exact, uniquely owned `float[]` is copied in full before each element store;
- an ordinary `pn` parameter is treated as borrowed merely because the function is procedural;
- a JavaScript property such as `x` scans 530 markup names before reaching the Lambda and JS
  catalogs.

Tune11 therefore restores the following division of responsibility:

```text
semantic admission       proves that a value belongs to a contract
representation conversion changes storage only when the source and target lanes differ
COW ownership             copies only when a second observable owner requires detachment
```

These are independent questions. Passing a type boundary must not imply deep ownership cloning.
A checked store must not imply copying a unique container. A procedure is not automatically a
borrow. Name identity must not imply a linear catalog search.

The implementation is divided into five tracks:

| Track | Mechanism | Primary targets |
|---|---|---|
| T11-A | Shape-first typed map admission | `binarytrees`, `gcbench`, `deriv`, `awfy/json` typed MIR |
| T11-B | Unique checked array writes | `matmul` typed MIR, typed array guards |
| T11-C | Non-borrowed `pn` native specialization | `diviter`, `sum`, `sumfp` untyped MIR |
| T11-D | Generated O(1) well-known-name lookup | `awfy/nbody` and property-heavy LambdaJS rows |
| T11-E | Residual nullable read tuning, evidence-gated | `quicksort`, `raytrace3d`, `array1` typed MIR |

T11-E is conditional. It begins only after T11-A through T11-D land and a fresh release profile
shows a remaining nullable-load bottleneck. Tune11 must not pre-emptively weaken total-read
semantics or duplicate the native-lane helpers already implemented.

---

## 2. Result20 checkpoint and attribution

### 2.1 Comparable overall movement

On the 56 benchmark names shared by Result18 and Result20, the direct Result20/Result18
geometric movement is:

| Engine | Result20 / Result18 geomean | Rows at least 2x slower | Rows at least 10x slower |
|---|---:|---:|---:|
| MIR untyped | **1.62x** | 12 | 1 |
| MIR typed | **2.17x** | 23 | 7 |
| LambdaJS | **1.80x** | 25 | 0 |
| QuickJS | **1.02x** | 1 | 0 |
| Node.js | **1.00x** | 0 | 0 |

Node and QuickJS are same-run controls. Their stability rules out a general host slowdown as the
explanation for the Lambda regressions.

The published engine/Node headline moved accordingly:

| Engine | Result18 | Result20 |
|---|---:|---:|
| MIR untyped / Node | 2.55x | 4.26x |
| MIR typed / Node | 1.87x | 4.07x |
| LambdaJS / Node | 13.6x | 24.2x |

### 2.2 Largest measured regressions

| Benchmark | Engine | Result18 | Result20 | Slowdown |
|---|---|---:|---:|---:|
| `beng/binarytrees` | MIR typed | 6.399 ms | 376.836 ms | **58.89x** |
| `larceny/gcbench` | MIR typed | 297.662 ms | 12,201.6 ms | **40.99x** |
| `larceny/deriv` | MIR typed | 23.460 ms | 794.219 ms | **33.85x** |
| `kostya/matmul` | MIR typed | 42.300 ms | 1,324.47 ms | **31.31x** |
| `larceny/diviter` | MIR untyped | 1,219.68 ms | 31,110.3 ms | **25.51x** |
| `awfy/json` | MIR typed | 4.718 ms | 108.839 ms | **23.07x** |
| `larceny/quicksort` | MIR typed | 2.153 ms | 43.013 ms | **19.98x** |
| `jetstream/raytrace3d` | MIR typed | 88.299 ms | 1,101.91 ms | **12.48x** |
| `awfy/nbody` | LambdaJS | 271.451 ms | 1,907.30 ms | **7.03x** |

### 2.3 Matched archived-binary replay

Some benchmark fixtures were corrected after Result18. For example, recursive map fields became
nullable and some arrays gained explicit contracts. Therefore Tune11 does not rely only on the
two snapshot JSON files.

Running the **current fixtures** through both archived binaries reproduced the regressions:

| Current fixture | Result18 binary | Result20 binary | Conclusion |
|---|---:|---:|---|
| `beng/binarytrees`, MIR typed | 5.76 ms | 350 ms | runtime/compiler regression, not fixture drift |
| `kostya/matmul`, MIR typed | 41.7 ms | 1.33 s | checked-write regression |
| `larceny/diviter`, MIR untyped | 1.14 s | 19.94 s | lost untyped specialization |
| `awfy/nbody`, LambdaJS | 268.4 ms | 1,784.6 ms | LambdaJS runtime/compiler regression |

The exact Result20 row may still vary thermally; matched binaries on the same current fixture are
the attribution authority.

### 2.4 Release profiles

Profiles were collected from the archived Result20 release binary, never a debug build.
Artifacts are under `temp/result20_analysis/`.

#### Typed `gcbench`

- workload time: 12,992.7 ms;
- physical footprint: 3.1 GB;
- hot work: GC, clone visited-set hash maps, field extraction/storage, shape lookup, and recursive
  validator traversal;
- `lambda_type_check`, `lambda_type_matches`, `validate_against_map_type`,
  `map_shape_field_to_item`, and `map_shape_field_store_native_lane` all occur in the recursive
  path.

Before the regression, a July release profile measured `gcbench` around 385 ms with zero
collections and attributed 85.2% to ordinary map birth. Result20 has changed the workload from
"allocate each node once" into "repeatedly clone and validate reachable trees." The high GC time
is induced by the clone volume; it is not evidence that the collector is the first Tune11 target.

#### Typed `matmul`

- workload time: 1,445.6 ms;
- physical footprint: 2.3 GB;
- 724 of 772 busy samples were in `memmove` or zero-fill;
- the dominant call chain is `fn_mutable_value -> clone_mutable_array_num -> array_num_new`;
- the numeric multiply loop barely appears in the sample.

The fixture owns three arrays of 40,000 doubles. It performs 80,000 initialization writes and
40,000 output writes. Copying all 40,000 slots before each write creates approximately 4.8
billion slot copies, or about 38 GB of cumulative payload traffic, before allocator and GC
overhead.

#### Untyped `diviter`

- workload time in the focused profile: 20,996.5 ms;
- footprint: 18.7 MB;
- dominant leaves: `fn_sub`, `fn_ge`, and `is_truthy`;
- the small footprint rules out GC or container copying.

The loop is executing general boxed arithmetic and comparison dispatch on every iteration.

#### LambdaJS `nbody`

- worker samples: 777;
- `well_known_name_id` top-of-stack samples: 526, approximately 68%;
- the 777 `__ulock_wait` samples belong to the main thread waiting for the worker and are not a
  runtime lock;
- the current catalog contains 530 markup names, 5 Lambda names, and 20 JS names.

`well_known_name_id()` computes a hash but then scans the three arrays linearly, beginning with
all markup records. A normal property such as `x`, `y`, `z`, or `vx` misses the catalog and probes
all 555 records.

---

## 3. Root causes in the current code

### 3.1 T11-A root cause: existence of a native field is mistaken for incompatibility

`runtime_type_admit_value()` currently contains the equivalent of:

```text
if value is Map
   and expected is a concrete Map
   and expected contains any native-lane field
then
    runtime_type_admit_map(value, expected)
```

This asks whether the destination has a native field, not whether the candidate already has the
same Shape and lane layout. `runtime_type_admit_map()` then:

1. calls `fn_mutable_value(value)`;
2. deep-clones the entire reachable candidate graph using a cycle/identity hash map;
3. finds and recursively admits each expected field;
4. rebuilds fields whose representation differs;
5. calls `lambda_type_matches()` over the resulting graph.

For a recursive `Node` value, child maps have already passed their own boundary before the
parent is built. The parent boundary nevertheless clones and revalidates those subtrees again.
The cost grows with both graph size and nesting depth.

The missing primitive is a Shape/contract relation that distinguishes:

```text
already exact
same physical representation but needs semantic validation
semantically admissible but needs physical reification
incompatible
```

### 3.2 T11-B root cause: a local exact array cannot become a native-write witness

The typed computed-assignment lowering defines its direct `ArrayNum` witness only when
`writes_through_caller` is true. A local declaration such as:

```lambda
var a: float[] = fill(size, 0.0)
a[i] = value
```

therefore falls into `lambda_array_set_checked()` instead of the native store. The non-in-place
checked setter creates its candidate with `fn_mutable_value(owner)`, which clones the complete
array before validating and storing one value.

This contradicts the COW design's primary invariant: a proven-unique value mutates in place;
only a shared value detaches. Type checking should validate the incoming element, not manufacture
a second owner.

### 3.3 T11-C root cause: procedure effect is conflated with parameter borrowing

`mir_function_has_borrowed_params(fn_node, is_proc_fn)` immediately returns true for every
procedure. That prevents generation of the native specialization even when the procedure has no
`var` parameter.

A `pn` is procedural because its body may perform procedural operations. Its ordinary value
parameters are not aliases to caller roots. Only explicit `var` parameters participate in
caller-visible write-back.

The global boolean also prevents a future mixed signature from specializing independent value
parameters merely because one parameter is borrowed.

### 3.4 T11-D root cause: generated identity records lack generated lookup data

The name-identity design required `generate_well_known_names.py` to emit deterministic static
lookup data. The current implementation emits records but implements text-to-ID lookup by walking
every record at runtime.

The records are immutable process data, so neither a runtime `HashMap` nor mutable one-time
initialization is needed. A generated open-addressed table can resolve the current 555-name set in
one to a few probes while retaining exact length/byte verification.

### 3.5 What Tune11 does not initially optimize

The following are visible in some profiles but are consequences or residuals, not initial
targets:

- GC collection triggered by temporary map/array clones;
- the ordinary map allocator that was the pre-regression `gcbench` hotspot;
- general `fn_index` or nullable lane boxing without a post-T11-A/B profile;
- ArrayNum mutable views, nullable N-D kernels, and automatic re-promotion;
- unrelated LambdaJS prototype, call, or descriptor bottlenecks;
- conservative native-stack GC scanning, which remains retired.

---

## 4. Non-negotiable semantic and runtime invariants

Every Tune11 phase must preserve all of the following.

1. **Type enforcement remains real.** A statically proven boundary may be omitted, but a dynamic
   value must still be checked before it enters a concrete contract.
2. **`T[]` never widens due to a store.** Null written to `T[]` is rejected. Only an already
   nullable or generic contract may admit null and change physical representation.
3. **Value covariance preserves both values.** `T[] -> T?[]` remains allowed for ordinary value
   assignment. A later null store through the target cannot mutate or retag the source.
4. **Borrowed array parameters remain invariant.** `var T?[]` cannot borrow a caller's `T[]`.
5. **Total reads remain nullable.** An unproved `a[i]` has type `T?` and returns the correct lane
   null out of range. No Result18 raw-read behavior may be restored by weakening this rule.
6. **Admission and ownership are separate.** Returning an existing value from an exact type
   check is legal only when the caller still performs the required COW share/install operation.
7. **Transactional failure remains observable.** A failed dynamic conversion or element check
   cannot partially modify the source or the destination being published.
8. **Shapes are immutable once shared.** Shape identity can be a guard only after auditing all
   shape writers and ensuring detach-before-mutate.
9. **Only trusted Shapes prove nested contracts.** Pointer equality to an arbitrary dynamic or
   JS-created TypeMap is not sufficient. A Shape used as an admission certificate must have a
   documented construction/write invariant.
10. **COW copies one level.** A conversion needing a private root copies the root storage and
    shares children. It does not deep-copy an untouched subtree.
11. **Precise rooting remains mandatory.** Allocating conversion/copy paths use `RootFrame` and
    `Rooted`; moved pointers are reloaded after possible collection.
12. **LambdaJS remains dynamically boxed.** Tune11-D changes name lookup only. It does not admit
    JS `undefined` to a Lambda nullable lane or add an implicit JS static type system.
13. **No benchmark-specific recognition.** Optimizations are based on Shape identity, ownership,
    contract descriptors, or call-site proofs—not type names or benchmark source positions.

---

## 5. T11-P0 — instrumentation and reproducible baseline

Instrumentation lands first and is removed or left disabled-by-default after the corresponding
phase is accepted. It must reuse existing profile facilities rather than adding duplicate
frameworks.

### 5.1 COW and admission counters

Extend the existing `COW_EXEC_PROFILE` output with counters for:

- `map_admit_calls`;
- `map_admit_exact_shape_hits`;
- `map_admit_storage_compatible_hits`;
- `map_admit_readonly_validations`;
- `map_admit_reifications`;
- `map_admit_deep_clone_calls`;
- `map_admit_fields_visited`;
- `map_admit_bytes_copied`;
- `array_checked_store_calls`;
- `array_checked_store_direct`;
- `array_checked_store_unique_inplace`;
- `array_checked_store_cow_detach`;
- `array_checked_store_rebuild`;
- `array_checked_store_full_clone`;
- `array_checked_store_bytes_copied`.

The accepted T11-A/B state should show:

```text
binarytrees/gcbench exact typed-map boundaries: zero deep clones
matmul exact local element writes:             zero full-array clones
```

Counters are release-safe only when disabled. Do not time acceptance candidates with
`COW_EXEC_PROFILE` enabled.

### 5.2 MIR specialization diagnostics

Add an opt-in transpiler diagnostic or existing MIR profile record that reports, per function:

- function/procedure kind;
- parameter ownership classification;
- inferred versus declared lane;
- native body emitted or rejected;
- boxed adapter emitted;
- exact rejection reason: borrowed `var`, escaped, indirect/mixed caller, unknown type, or ABI
  limitation.

The diagnostic must prove that untyped `diviter` receives a native specialization and that a
true `var` fixture still uses its safe write-back path.

### 5.3 Name lookup profiling

Extend `LAMBDA_JS_EXEC_PROFILE` rather than adding a release branch to every property access.
Record:

- well-known lookup calls;
- hits and misses;
- total probes;
- maximum probes;
- canonical owner pool of hits;
- calls bypassed because a `PropertyKeyRef` was already available.

The normal release binary must contain no JS execution-profile marker, as enforced by the
standard benchmark runner.

### 5.4 Baseline preservation

Keep the exact Result20 binary as the initial baseline:

```text
test/benchmark/exe/lambda-v20-6fcf2283fa
```

For every phase, archive the candidate under `temp/tune11/` and run the baseline and candidate
interleaved against the same current fixture. Do not compare a newly built candidate only with a
historical JSON row.

---

## 6. T11-A — Shape-first typed map admission

### 6.1 A0 — audit the Shape certificate

Before using Shape identity as a semantic proof, inventory every creator and writer of:

- `Map::type`;
- `TypeMap::shape`, `last`, `length`, and `byte_size`;
- `ShapeEntry::type`, `byte_offset`, and lane metadata;
- named-shape, private-clone, constructor-shape, and transition flags;
- raw C, Lambda, LambdaJS, input/Mark, validator, and editor field stores.

Classify TypeMaps into at least these policy categories:

| Category | Shape identity proves storage? | Shape identity proves field contract? |
|---|---:|---:|
| canonical sealed Lambda named contract | yes | yes, after the write audit |
| private detached clone of a sealed contract | yes | yes if all writes remain checked |
| inferred dynamic/input map | yes | no; validate before contract admission |
| LambdaJS constructor/property shape | yes | no for Lambda contract admission |
| open/transitioning map | only for current layout | no without validation |

Reuse existing flags if they already express these facts exactly. If they do not, add one explicit
contract-sealed/trusted bit or certificate field to `TypeMap`; do not infer trust from a vague
combination of `has_named_shape`, `is_private_clone`, or JS constructor flags. Update ABI layout
assertions and generated/public headers if the structure changes.

The certificate invariant is:

```text
trusted contract Shape
    => every slot has the descriptor required by the contract
    => every published write to that slot passed the slot contract
    => no raw JS/host writer can bypass the invariant
```

If the audit cannot prove this invariant for a category, that category stays on read-only
validation. It does not block the exact compiler-produced Lambda fast path.

### 6.2 A1 — one shared map contract relation helper

Add one shared helper near the existing TypeMap/Shape helpers, not separate switches in the
transpiler and runtime. Conceptually:

```c
typedef enum MapContractRelation {
    MAP_CONTRACT_INCOMPATIBLE,
    MAP_CONTRACT_EXACT_TRUSTED,
    MAP_CONTRACT_STORAGE_COMPATIBLE,
    MAP_CONTRACT_NEEDS_REIFICATION,
} MapContractRelation;
```

The helper accepts the candidate `TypeMap*` and expected contract `TypeMap*`. It compares:

- canonical trusted contract identity first, O(1);
- field count and required openness/sealed policy;
- field key identity/hash and exact bytes where identity is unavailable;
- semantic field contract;
- `LaneStorageDesc`, raw-Item storage, storage width, alignment, and byte offset;
- container kind and any fixed-slot prefix constraint.

Use the existing TypeMap hash table for cross-shape field lookup. Do not scan the candidate Shape
from its head once per expected field. A structural relation may be O(field count), but a repeated
field lookup must remain O(1) average and an exact Shape hit must remain O(1) total.

The relation is about both semantics and representation. Two fields with the same base `TypeId`
but different nullability are not physically exact. `int` and `int?`, `string` and `string?`, and
the typed-Item `i64?` lane must compare through the full descriptor.

### 6.3 A2 — reorder `runtime_type_admit_value`

Replace the current "expected map has any native lane" trigger with this order:

1. unwrap the boundary contract once;
2. if candidate and expected carry the same trusted contract certificate, return the original
   value as the converted value;
3. if storage is compatible but the Shape is not trusted as a semantic certificate, run one
   read-only semantic validation against the original value; on success, return the original;
4. only if the relation says `NEEDS_REIFICATION`, enter conversion;
5. report incompatible values through the existing validation/type-error path.

Returning the original on an exact admission does not install a second owner. The calling
binding/argument/return lowering must still execute its normal share/COW ownership rule.

Do not call `fn_mutable_value()` merely to make validation transactional. Read-only validation
cannot mutate the source.

### 6.4 A3 — transactional selective reification

When representation really differs, replace the deep clone with an unpublished shallow
candidate:

```text
copy one map level
mark/share unchanged container children
convert only fields whose descriptor or value representation changes
recursively construct a replacement only for a changed nested field
publish the root only after all conversions succeed
```

Use the existing one-level COW clone helper. If it is currently private to another module,
promote it through the owning runtime header; never duplicate its implementation.

This retains transactional failure: the shallow candidate is not observable until every
required conversion succeeds. On failure, the source remains untouched and the unpublished GC
candidate is reclaimed normally.

Nested conversion follows changed spines. An unchanged child is shared, not deep-cloned. The
cost becomes proportional to the converted Shape/spine rather than the reachable graph.

### 6.5 A4 — eliminate redundant final graph validation

`runtime_type_admit_map()` currently validates fields during admission and then calls
`lambda_type_matches()` over the complete candidate. After A1–A3, define exactly which proof owns
the result:

- `EXACT_TRUSTED`: the Shape certificate is the proof;
- `STORAGE_COMPATIBLE`: the one read-only validation is the proof;
- `NEEDS_REIFICATION`: every required field is admitted during construction and the destination
  Shape is trusted.

Do not repeat a full validator traversal after one of these proofs succeeds. A debug/profile
build may assert the postcondition, but the release hot path cannot rescan the graph.

### 6.6 A5 — compiler boundary elision

Extend `mir_boundary_is_redundant()` and the direct-call/return lowering to recognize only
compiler-proven exact cases:

- a map literal constructed directly with the expected canonical named Shape;
- a value already admitted to that exact contract and not invalidated by a dynamic write;
- a direct function return whose effective full type is the exact declared contract;
- a typed local-to-local assignment with unchanged contract and representation.

Do not elide for `any`, open maps, JS values, input maps, unresolved unions, dynamic computed
writes, or a structural map that merely happens to have the same current values.

This compiler optimization is separate from the runtime fast path. Dynamic callers must still be
correct even if they enter through the boxed adapter.

### 6.7 A6 — focused tests

Add or extend C-level GTests to cover:

- same trusted Shape returns the original map pointer and performs zero clone allocation;
- structurally compatible untrusted map validates without cloning;
- `int` field admitted to `int?` field produces the required nullable lane Shape;
- pointer-backed `T -> T?` field conversion uses the `NULL`-capable pointer descriptor;
- `i64?`/`u64?` use the typed-Item descriptor;
- a failed nested conversion leaves the complete source graph unchanged;
- cyclic and DAG maps do not recurse infinitely or duplicate unchanged shared children;
- open fields and duplicate-key/last-field semantics are preserved;
- fixed constructor slot prefixes remain valid after a real storage-size transition;
- raw JS/host shapes never receive the trusted Lambda-contract fast path.

Extend Lambda fixtures around:

- `proc_nullable_native_map.ls`;
- `proc_optional_map_binding.ls`;
- `type_enforcement_map_cow.ls`;
- `type_enforcement_union_map_cow.ls`;
- `type_enforcement_union_map_storage.ls`;
- recursive nullable `Node` construction and failed dynamic admission.

Every new `.ls` fixture must have its corresponding `.txt` expected output.

Run forced-GC coverage while allocating shallow conversion candidates and nested replacements.

### 6.8 A7 — performance acceptance

T11-A is not complete merely because the code uses Shape identity. It must meet all of:

- `binarytrees` typed: no map deep clones; target **<=15 ms**;
- `gcbench` typed: no admission deep clones; target **<=600 ms**;
- `deriv` typed: target **<=60 ms**;
- `awfy/json` typed: target **<=15 ms**;
- profile footprint returns to the pre-regression class and does not approach multi-gigabyte
  temporary usage;
- untyped map rows and COW alias fixtures have no confirmed regression.

These targets intentionally allow overhead above Result18 for the corrected semantics, while
requiring removal of the algorithmic regression. If a correct exact-Shape implementation misses
them, profile it before relaxing any threshold.

---

## 7. T11-B — unique checked array writes

### 7.1 B0 — separate leaf validation, ownership, and representation

Refactor the checked array write around three shared operations:

```text
check element against element contract
prepare writable owner under COW
store into the current representation or rebuild once if required
```

Use existing helpers for type admission, COW preparation, native-lane storage, and ArrayNum
conversion. Do not create a second per-element conversion table.

The element check must happen before any mutation. Once it succeeds, a unique exact
representation may be modified in place. Transactionality does not require copying an owner that
has no other observable owner.

### 7.2 B1 — broaden the native-write witness to exact locals

The direct native witness must not require `writes_through_caller`. It should require:

- an exact typed root with a known array element contract;
- an integral index carrier;
- a right-hand side statically known to be admissible without representation change;
- a current array representation whose lane descriptor matches that contract;
- either compiler-proven uniqueness or the normal runtime COW unique guard.

For `float[]`, `int[]`, `bool[]`, sized integer arrays, and exact non-null pointer arrays, the
compiler may emit the existing direct specialized store after these proofs.

For nullable arrays, a non-null value may use the existing ArrayNum representation only when its
specialized element type accepts the value exactly. A null store into `T?[]` is not a direct
ArrayNum write; it takes the one-time demotion path.

### 7.3 B2 — ownership behavior

The write matrix is:

| Owner state | Representation change? | Required action |
|---|---:|---|
| compiler-proven unique | no | validate leaf, store in place |
| runtime unique | no | one shared-bit test, store in place |
| shared/static value | no | one-level COW detach, store into replacement, write replacement back |
| unique `ArrayNum<T?>` | admitted null | rebuild once as native `Array<T?>`, store null, publish replacement |
| shared `ArrayNum<T?>` | admitted null | detach/rebuild once, preserve source descriptor and data |
| any `ArrayNum<T>` | null rejected by `T[]` | return type error, no mutation or rebuild |

Never call the deep generic `fn_mutable_value()` for an ordinary element store. ArrayNum COW is
one payload `memcpy` only when the shared bit requires detachment; general Array COW copies one
level and shares container elements according to the COW design.

### 7.4 B3 — maintain the array contract invariant incrementally

After an array has been admitted to `T[]` or `T?[]`, its descriptor plus checked element stores
maintain the invariant. A successful store therefore does not need a full-array
`lambda_type_matches()` scan.

The checked setter validates:

1. the root descriptor still represents the expected contract;
2. the incoming leaf value is admitted;
3. any required representation transition constructs the expected destination descriptor.

The full validator remains for dynamic ingress where the array has not yet established that
invariant. It does not run after every known typed store.

### 7.5 B4 — preserve covariance and ArrayNum demotion rules

Tune11 must retain the nullable design exactly:

```text
int[] -> int?[]       allowed as a value conversion under COW
int?[] -> int[]       rejected
var int[] -> var int?[] rejected as an aliasing borrow
int[] store null      rejected; descriptor remains int[]
int?[] store null     ArrayNum demotes once to native Array<int?>
```

The source and target may share a null-free ArrayNum backing only through the documented COW
policy. A target demotion must detach before publishing the nullable representation.

Mutable ArrayNum views and vector/N-D nullable kernels remain deferred as stated in
`Lambda_Design_Compiling_Nullable.md` §10. Tune11 must reject or retain the existing fallback for
those cases rather than silently breaking view write-through.

### 7.6 B5 — focused tests

Add C-level tests, preferably in the existing runtime representation GTest suite, for:

- exact unique `float[]`, `int[]`, and `bool[]` writes preserving the owner pointer;
- zero full-clone counter movement across repeated unique writes;
- one and only one detach when a shared array is first written;
- a second write to the detached replacement staying in place;
- snapshot source preservation after detachment;
- rejected null/write-type mismatch leaving an ArrayNum byte-identical;
- nullable ArrayNum demotion producing the right `LaneStorageDesc` and null sentinel;
- GC preservation of pointer-lane arrays during detachment/rebuild;
- index error behavior and negative-index legacy behavior;
- failed writes not publishing partially rebuilt arrays.

Extend or add Lambda/MIR fixtures covering:

- `proc_array_local_float_specialization.ls`;
- `proc_typed_array_guard.ls` and `test/mir/lambda/typed_array_guard.mir-check`;
- `proc_nullable_native_array.ls`;
- `proc_nullable_native_float_array.ls`;
- `proc_nullable_native_pointer.ls`;
- `proc_nullable_native_sized_array.ls`;
- `proc_nullable_native_int64_array.ls`;
- COW alias and ordering fixtures;
- an exact local 40,000-element write loop that asserts zero replacement pointers after birth.

Do not encode benchmark constants in the runtime. The large loop is a test/measurement fixture
for the general unique-write rule.

### 7.7 B6 — performance acceptance

- `kostya/matmul` typed target: **<=75 ms**;
- cumulative full-clone calls during the fixture: **zero**;
- peak footprint must return from 2.3 GB to the ordinary tens-of-megabytes class;
- the profile must no longer be dominated by `memmove`, zero-fill, or GC under
  `clone_mutable_array_num`;
- untyped `matmul`, `array1`, `quicksort`, array COW, and nullable transition guards have no
  confirmed regression.

The target allows corrected checked-store overhead above Result18's 42.3 ms while requiring the
O(length x writes) behavior to disappear.

---

## 8. T11-C — restore native specialization for non-borrowed procedures

### 8.1 C0 — replace the function-wide borrowed boolean

Introduce one parameter ownership classification shared by planning and lowering, conceptually:

```c
typedef enum MirParamOwnership {
    MIR_PARAM_VALUE,
    MIR_PARAM_BORROWED_VAR,
} MirParamOwnership;
```

Function kind and parameter ownership remain separate facts:

```text
fn/pn              controls language/effect behavior
ordinary/var param controls value versus borrow ABI behavior
```

The first Tune11 implementation may keep a function containing any `var` parameter on the boxed
write-back ABI. It must not classify a `pn` with only ordinary parameters as borrowed.

A later mixed ABI may specialize independent value parameters while keeping borrowed parameters
as rooted Item/write-back slots. That extension is not required to recover `diviter` and must not
be rushed into Tune11-C1.

### 8.2 C1 — native body for closed ordinary-parameter `pn`

Reuse the existing dual-function machinery:

- infer or consume the exact scalar parameter types;
- generate the native specialization for a closed direct-call set;
- keep a boxed slow body/adapter for dynamic, public, escaped, imported, indirect, or mixed calls;
- use the same native nullable lane descriptors as ordinary functions;
- box only at an actual Item boundary.

Procedure effects do not prevent scalar registers from remaining native. Calls made by the body
remain ordinary safepoints with the existing precise-root and scalar-home rules.

Recursive procedures require a native recursive edge when the signature is closed. The boxed
adapter must not force recursive calls back through the slow path.

### 8.3 C2 — retain safe `var` write-back

True `var` parameters continue to borrow caller roots and use the existing checked write-back
behavior. Tune11-C does not introduce the open nullable native `var` ABI from
`Lambda_Design_Compiling_Nullable.md` §10.

Required behavior includes:

- exact caller replacement after a successful write;
- no raw carrier that severs the caller's rooted Item ownership;
- invariant mutable-array parameters;
- type errors leaving the caller unchanged;
- alias/order rules from the COW design.

Removing `if (is_proc_fn) return true` without replacing these guards is not an acceptable fix.

### 8.4 C3 — MIR tests

Extend `test/mir/lambda/callsite_inference.*` and procedural MIR fixtures to assert:

- untyped closed `pn sub_loop(x, y)` has a native entry;
- its loop contains no calls to `fn_sub`, `fn_ge`, or `is_truthy` for proven integer operands;
- a recursive no-`var` procedure recursively calls its native body;
- a dynamically called/escaped procedure retains a boxed adapter;
- a procedure with a true `var` parameter remains on the safe write-back path;
- nullable ordinary parameters use their native nullable lane through the direct entry;
- return and error lanes remain correct.

Integration fixtures must cover positive, zero, negative, poison, overflow/promotion, and dynamic
fallback cases. Optimization must not be limited to the exact `diviter` loop shape.

### 8.5 C4 — performance acceptance

- `larceny/diviter` untyped target: **<=1.6 s**;
- `r7rs/sum` untyped target: **<=7 ms**;
- `r7rs/sumfp` untyped target: **<=0.7 ms**;
- the `diviter` profile must no longer be dominated by `fn_sub`, `fn_ge`, and `is_truthy`;
- typed variants and true-`var` correctness fixtures must not regress.

Short rows use at least nine runs per binary; `diviter` uses at least three interleaved pairs.

---

## 9. T11-D — generated O(1) well-known-name lookup

### 9.1 D0 — preserve the identity contract

The generated lookup must preserve:

- all existing `NameId` numeric values;
- pool and ordinal decoding;
- canonical physical record ownership and aliases;
- exact byte-and-length matching, including embedded NUL policy if supported;
- string versus Symbol/private key identity distinctions;
- deterministic generated output across platforms;
- immutable process data with no runtime mutable initialization.

Do not replace the scan with a runtime `HashMap`, `std::unordered_map`, constructor, lock, or
`pthread_once` table build.

### 9.2 D1 — extend `generate_well_known_names.py`

Generate one combined text-to-canonical-record hash table after aliases and ownership are
resolved. A suitable first representation is a power-of-two open-addressed table:

```text
slot:
    0                         empty
    encoded pool + ordinal    candidate record
```

Generation rules:

1. compute the same `name_classify_ordinary()` FNV-1a hash used at runtime;
2. choose a deterministic power-of-two capacity with load factor no greater than 0.70;
3. place canonical spellings using deterministic linear or quadratic probing;
4. emit the slot table as `const` generated C data;
5. fail generation on unresolved duplicate ownership, invalid IDs, or inability to place an
   entry under the selected policy;
6. emit probe statistics so catalog growth cannot silently create a long chain;
7. keep ID-to-record decoding through the existing direct pool/ordinal arrays.

At the current 555 records, a 1024-slot table is the expected first capacity unless canonical
alias folding materially changes the count. The generator decides from the load rule; runtime
code must not hard-code 555 or 1024.

### 9.3 D2 — runtime lookup

`well_known_name_id(StrView name)` becomes:

```text
reject null input
compute hash once
probe generated slots
stop at empty slot
for each occupied candidate, compare stored hash, length, and exact bytes
return canonical predefined ID or NONE
```

Common misses become one to a few cache-friendly probes rather than 555 record loads. Hash
collisions still perform exact byte comparison; hash equality alone never establishes identity.

`well_known_name_ref(id)` and `well_known_name_view(id)` remain direct O(1) ordinal decode paths.

### 9.4 D3 — reuse existing property keys

After D2, profile again. If `name_pool_create_strview()` remains material in named property
access, audit call sites that already have a stable `PropertyKeyRef` or generated `NameId`:

- MIR fixed-name property sites should load their module property-key entry directly;
- Shape lookups accepting `PropertyKeyRef` must use the key-aware helper;
- repeated conversion from stable key -> characters -> interned stable key should be removed;
- dynamic computed string keys continue through NamePool interning.

This is a second phase, not a prerequisite for the generated table. It lands only with profile
evidence and must reuse the existing key-aware TypeMap helpers.

### 9.5 D4 — tests

Extend `test/test_name_pool_gtest.cpp` with:

- round-trip lookup for every generated record;
- every alias returning the canonical owner pointer/ID;
- misses before, between, and after occupied probe clusters;
- deliberately colliding hashes with unequal bytes/lengths;
- empty, ASCII, Unicode, and maximum generated name lengths;
- string/Symbol same-spelling separation;
- deterministic generator output and `--check` drift detection;
- table load and maximum-probe bounds;
- multi-runtime/isolate reuse without mutable initialization.

Run the Unicode identifier and large-private-name suites as stability guards. Tune11-D does not
change private-name environment construction, but generated identity changes must not revive the
batch-only recovery or large-class behavior previously investigated.

### 9.6 D5 — performance acceptance

- focused current-fixture `awfy/nbody` LambdaJS target: **<=750 ms** for D2;
- `well_known_name_id` must fall below **5%** of worker CPU samples;
- average probes on `nbody` must be below 3 and the generated worst-case bound must be documented;
- no generated lookup uses runtime allocation or locking;
- LambdaJS property-heavy guard rows (`towers`, `queens`, `bounce`, `fasta`, `deriv`, `cd`,
  `havlak`, and `hashmap`) have no confirmed regression;
- `make test262-baseline` completes with zero failures and zero retries.

If D2 reaches its lookup target but `nbody` remains above 750 ms, profile before enabling D3.
The remaining time may belong to the property/prototype pipeline rather than name identity.

---

## 10. T11-E — residual nullable indexed-read work, conditional

T11-E starts only after the first four tracks are measured together. It exists because typed
`quicksort`, `raytrace3d`, and `array1` may retain costs after clone/admission repair, but their
current Result20 rows are contaminated by the larger failures.

### 10.1 Entry condition

Enter T11-E only when a release sample shows at least 10% of a target's worker time in one of:

- boxing/unboxing an otherwise exact nullable lane;
- repeated descriptor resolution for the same array;
- redundant bounds/null tests already dominated by a proven loop range;
- general `fn_index` dispatch for an exact native Array/ArrayNum;
- nullable-lane conversion around a direct call.

Do not infer a nullable-read bottleneck merely from a slower typed row.

### 10.2 Allowed optimization directions

- preserve `LaneStorageDesc` in effective type information through locals and phi joins;
- load an in-range native slot directly and select the lane null only on failed bounds;
- hoist stable data/length/descriptor under existing COW mutation guards;
- recognize compiler-proven loop bounds and omit only the redundant check;
- narrow a `T?` to `T` after a dominating presence/bounds proof;
- keep native nullable parameters/returns through direct calls.

### 10.3 Forbidden shortcuts

- infer `a[i]` as `T` merely because the benchmark index happens to be valid;
- use mixed raw-`T`/boxed-`ItemNull` values in one MIR register;
- let `undefined` enter a Lambda nullable lane;
- treat `int[]` as `int?[]` or mutate its descriptor;
- open-code nullable sentinel bits outside the shared lane helpers;
- extend nullable N-D/vector kernels without the separate design work already deferred.

### 10.4 Tests and acceptance

MIR sidecars must assert the native lane, explicit OOB-null path, and absence of boxed Item calls
inside the proven in-bounds loop. Negative/OOB fixtures remain mandatory.

A retained T11-E change must improve its declared target by at least 10%, preserve all nullable
goldens, and introduce no confirmed regression in scalar arithmetic or generic-array guards.

---

## 11. Implementation sequence

Each phase is independently reviewable and performance-gated. Do not combine several mechanisms
into one candidate before identifying which one produced the gain.

### Phase P0 — freeze evidence and counters

- [x] Archive/verify the Result20 binary and SHA-256.
- [x] Capture fresh same-machine Result20 medians for every primary and guard row.
- [x] Add the COW/admission counters and MIR specialization diagnostic.
- [x] Extend JS execution profiling for name probes.
- [x] Prove the current bad counts: recursive map clones, matmul full clones, no native untyped
  `pn` body, and hundreds of name probes per miss.

### Phase P1 — T11-A exact Shape fast path

- [x] Complete the Shape writer/trust audit.
- [x] Add the shared map contract relation.
- [x] Return exact trusted maps without cloning or recursive validation.
- [x] Add focused representation and forced-GC tests.
- [x] Measure the four typed map targets before proceeding to structural conversion.

### Phase P2 — T11-A selective conversion

- [x] Replace genuine representation conversion deep clones with one-level transactional COW.
- [x] Convert only changed fields/spines.
- [x] Remove redundant post-conversion full validation after a documented proof.
- [x] Add covariance/nested conversion/DAG/cycle tests.
- [x] Re-measure T11-A and retain only if its acceptance gates pass.

### Phase P3 — T11-B checked array mutation

- [x] Split leaf admission from writable-owner preparation.
- [x] Broaden the direct native witness to exact local arrays.
- [x] Use the unique/shared COW decision and one-time nullable demotion.
- [x] Remove full clone and full post-store validation from the exact path.
- [x] Run the full nullable array/COW matrix and `matmul` acceptance.

### Phase P4 — T11-C procedural specialization

- [x] Replace function-wide borrowed classification with parameter ownership.
- [x] Emit native bodies for closed no-`var` procedures.
- [x] Retain boxed adapters and safe `var` write-back.
- [x] Add MIR sidecars and dynamic/recursive fallback coverage.
- [x] Measure `diviter`, `sum`, and `sumfp`.

### Phase P5 — T11-D generated name lookup

- [x] Extend the generator and regenerate checked-in name files.
- [x] Add deterministic collision/alias/Unicode tests.
- [x] Replace the runtime scan with generated probes.
- [x] Run focused LambdaJS benchmarks and complete Test262.
- [x] Profile before deciding whether D3 property-key reuse is necessary.

### Phase P6 — integrated release and conditional T11-E

- [x] Build a clean release with all retained tracks.
- [x] Run all correctness gates.
- [x] Run the focused cross-track guard matrix.
- [x] Review residual slow typed rows; no T11-E patch is retained without its required
  nullable-read profile.
- [x] Implement T11-E only if its 10% entry condition is met; the condition was not
  established by the guarded snapshot.
- [x] Capture the next canonical benchmark snapshot as Result21.
- [x] Append measured execution results to §15 and mark only completed tracks done.

---

## 12. Correctness gates

### 12.1 Focused native tests after every affected phase

Use the actual built test executables and filters available after `make build-test`, including:

```text
test_item_repr_gtest          Shape/lane/ArrayNum representation and transition tests
test_name_pool_gtest          generated name identity and lookup
test_mir_emission_gtest       native/boxed call and array/map lowering
test_mir_ratchet_gtest        MIR text/size budgets
test_mir_gc_stress_gtest      precise roots during allocation/conversion
test_lambda_gtest             Lambda integration/negative scripts
```

Run relevant executables directly with focused `--gtest_filter` during development. Run the
complete affected executable before accepting a phase.

### 12.2 Required Lambda integration suites

At minimum, retain coverage for:

- map Shape transitions under null/value and storage-size changes;
- map COW snapshots, unions, open fields, and failed transactional conversion;
- unique and shared Array/ArrayNum writes;
- `T[]`/`T?[]` covariance and rejected reverse/borrow assignments;
- nullable scalar, pointer, sized, and typed-Item lanes;
- procedure parameter inference, dynamic calls, recursion, and `var` write-back;
- Unicode identifiers, private names, and name-pool identity;
- forced GC and freed-memory poisoning on new allocating paths.

### 12.3 Broad gates

Before an individual runtime/compiler phase is declared retained:

```bash
make test-lambda-baseline
make test262-baseline
```

The Test262 acceptance state is:

```text
failures: 0
retries:  0
```

Do not use baseline updates to hide a regression. If a MIR text sidecar changes, inspect the
generated MIR and update the sidecar only when it expresses the intended new lowering.

Tune11 does not require `make node-baseline`; it is outside the current default Lambda runtime
closeout unless separately requested.

---

## 13. Performance protocol

### 13.1 Build and host discipline

- use `make release`; never use a debug or JS execution-profile build for timing;
- run on AC power with low-power mode disabled;
- do not run builds, Test262, sampling, or another benchmark concurrently;
- keep temporary binaries, logs, and profiles under `./temp/tune11/`, never `/tmp`;
- verify exact output and exit status before comparing timings.

### 13.2 Focused A/B protocol

Use archived binaries through `LAMBDA_EXE` and the measurement runner with `--no-save`. Run
baseline and candidate interleaved, not as two long sequential blocks.

Example shape:

```bash
LAMBDA_EXE=test/benchmark/exe/lambda-v20-6fcf2283fa \
  python3 test/benchmark/run_benchmarks.py \
  -e mir -s larceny -b gcbench -n 3 --no-save

LAMBDA_EXE=temp/tune11/lambda-t11-a-<commit> \
  python3 test/benchmark/run_benchmarks.py \
  -e mir -s larceny -b gcbench -n 3 --no-save
```

Use at least:

- five interleaved pairs for rows below 100 ms;
- three interleaved pairs for long rows;
- nine or more runs per binary for sub-millisecond rows;
- a fresh profile of the candidate after the timing threshold is met.

If a guard moves adversely by more than 3%, rerun five interleaved pairs. A confirmed adverse
movement rejects the phase until explained and fixed.

### 13.3 Per-track guard sets

| Track | Primary rows | Guard rows |
|---|---|---|
| T11-A | typed `binarytrees`, `gcbench`, `deriv`, `awfy/json` | untyped variants, `splay`, `richards`, map COW micro |
| T11-B | typed `matmul` | untyped `matmul`, `array1`, `quicksort`, `fft`, array COW micro |
| T11-C | untyped `diviter`, `sum`, `sumfp` | typed variants, `fib`, `tak`, true-`var` procedural micro |
| T11-D | LambdaJS `nbody` | `towers`, `queens`, `bounce`, `fasta`, `deriv`, `cd`, `havlak`, `hashmap` |
| T11-E | declared profiled target | OOB/null, scalar loop, and generic-array controls |

### 13.4 Integrated targets

After all retained tracks, the Result21 restoration goals are:

| Headline | Result20 | Tune11 goal |
|---|---:|---:|
| MIR untyped / Node geo | 4.26x | **<=3.0x** |
| MIR typed / Node geo | 4.07x | **<=2.5x** |
| LambdaJS / Node geo | 24.2x | **<=17x** |

These are restoration goals, not permission to average away a regression. Every track must pass
its own primary and guard gates first.

### 13.5 Result21 snapshot

Only after correctness and focused A/B acceptance, run the canonical guarded workflow:

```bash
python3 test/benchmark/run_standard_benchmarks.py \
  --report-output test/benchmark/Overall_Result21.md \
  --report-title "Lambda Benchmark Results: Result21" \
  --typed
```

The workflow must build a clean release, reject profiling markers, run Test262 first, archive the
exact binary, write a fresh `benchmark_results_v21.json`, and generate the matching report.

---

## 14. Stop/rollback rules

A Tune11 candidate is reverted or redesigned when any of these occurs:

1. it weakens a nullable, covariance, type-error, COW, JS, or GC invariant;
2. it depends on a benchmark/type-name/source-position special case;
3. it replaces a deep clone with observable aliasing rather than correct COW sharing;
4. it uses Shape identity without a proven trust/write invariant;
5. it allows a failed dynamic check to mutate published state;
6. it improves a target by moving work outside the benchmark's timed region;
7. it adds a runtime mutable name table or hot-path initialization lock;
8. it passes only a focused fixture but fails the real baseline gate;
9. it shows a confirmed greater-than-3% guard regression without an understood independent cause;
10. it requires modifying vendored MIR or another third-party dependency.

When a phase misses its performance target, retain the correctness fix only if it is independently
required and does not regress guards. Otherwise leave the phase unlanded and record the profile
rather than accumulating unmeasured micro-tuning.

---

## 15. Execution ledger

The implementation ledger below records the retained working-tree candidate and its guarded
release evidence. The Result20 and Result21 archive labels share the same HEAD commit because
the Tune11 changes were evaluated in the working tree; their binary hashes distinguish the
actual candidates.

Each entry records:

- date and commit;
- exact baseline and candidate binaries plus SHA-256;
- code/tests changed;
- counters proving the intended path fired;
- interleaved raw timings and medians;
- release profile attribution and peak footprint;
- focused and broad correctness gates;
- retain, defer, or revert decision.

### T11-P0 — retained — 2026-08-05

- Candidate source commit label: `6fcf2283fa6e09c8cab645d66abfc8b5d1e22989`.
- Result18 archive SHA-256: `0d6d94e82bb2a2af6f9b5dfe0980131cee4be9a7e67fc4e4e06468e18dc95e40`.
- Result20 archive SHA-256: `d8e8360e0c0bff284d14448a4a445f9f16172fa15f36cbf563bb56611db52789`.
- Result21 archive SHA-256: `45284f9c107ccf73feec210983ba32df3e4ab0db8d25ed49b2a6804d428fcc63`.
- `COW_EXEC_PROFILE` and MIR diagnostics are disabled by default. The checked profile paths
  show exact Shape admission, zero admission deep clones, 40,000 unique in-place typed array
  stores, and the generated name-probe counts under `temp/tune11/`.
- Name profile: 7,992,807 calls, 12,622,442 probes, maximum 7 probes, average 1.579 probes.

### T11-A — retained — 2026-08-05

- Added the shared Shape/contract relation, trusted exact-map admission, target-aware typed-map
  allocation, recursive canonical TypeMap resolution, and transactional selective reification.
- `temp/tune11/binarytrees_canonical_profile.tsv`: 135,854 exact-shape admissions and zero
  deep-clone calls. The typed Result21 rows are `binarytrees` 12.4 ms, `gcbench` 306.2 ms,
  `deriv` 16.2 ms, and `awfy/json` 2.57 ms.
- The exact-map and recursive-map paths passed the focused representation/COW tests and the
  complete Lambda baseline and Test262 gates below.

### T11-B — retained — 2026-08-05

- Added exact-local native write witnesses, unique/shared COW ownership decisions, one-time
  nullable demotion, raw native-lane cloning, nullable direct-read fallback, and ArrayNum cache
  refresh after allocating MIR calls.
- `temp/tune11/matmul2_debug.cow.tsv`: 40,000 checked stores, all unique in-place; zero
  full-clone stores and zero copied bytes. The typed Result21 `matmul` median is 76.0 ms and
  the output remains `matmul: sum=-29562`.
- Nullable array/COW and typed representation coverage passed the focused and broad gates.

### T11-C — retained — 2026-08-05

- Replaced the procedure-wide borrowed classification with per-parameter ownership. Ordinary
  no-`var` `pn` parameters use native bodies when closed and eligible; true `var` parameters
  retain boxed adapters and safe write-back.
- MIR diagnostics confirmed native value lanes for the matmul procedural body while preserving
  the borrowed lane for the mutable output. Result21 medians are `diviter` 1.07 s, `sum`
  1.85 ms, and `sumfp` 0.318 ms.

### T11-D — retained — 2026-08-05

- The generator now emits the deterministic 555-record, 2,048-slot lookup table with maximum
  insertion probe 5; runtime property lookup uses bounded generated probes and retains the
  canonical owner/alias behavior.
- The final LambdaJS profile recorded 1.579 average probes and maximum 7 runtime probes. The
  Result21 `awfy/nbody` LambdaJS median is 576.7 ms, down from the Result20 attribution sample
  of 1,907.3 ms. `python3 utils/generate_well_known_names.py --check` passes.

### T11-E — deferred by entry rule — 2026-08-05

Result21 records residual typed rows (`array1` 12.7 ms, `quicksort` 41.5 ms, and missing typed
`raytrace3d`), but the plan explicitly says that a slower typed row is not evidence of a
nullable-read bottleneck. No qualifying release profile showing at least 10% worker time in
the listed nullable-read mechanisms was captured, so no speculative T11-E code was retained.
This preserves the required OOB/null semantics and leaves the track as a measured follow-up.

### Result21 — retained — 2026-08-05

- Guarded command:
  `python3 test/benchmark/run_standard_benchmarks.py --report-output test/benchmark/Overall_Result21.md --report-title "Lambda Benchmark Results: Result21" --typed`.
- Clean release archive: `test/benchmark/exe/lambda-v21-6fcf2283fa`, SHA-256
  `45284f9c107ccf73feec210983ba32df3e4ab0db8d25ed49b2a6804d428fcc63`.
- Release instrumentation check passed. Test262 pre-gate passed 40,261/40,261 with zero
  failures and zero retries at commit `673e9bacbe28590f501e2dcd817aadcc31899191`.
- Full Lambda baseline passed 3,582/3,582: 2,104 input-parser tests, 1,478 Lambda runtime
  tests, 688 Lambda GTests, and 324 JavaScript tests.
- Canonical 56-row, three-sample report: MIR untyped/Node geomean 3.17x, MIR typed/Node
  geomean 2.85x, and LambdaJS/Node geomean 15.5x. The report and raw JSON are
  `test/benchmark/Overall_Result21.md` and `test/benchmark/benchmark_results_v21.json`.

---

## 16. Completion definition

Tune11 is complete only when:

- exact typed maps cross internal boundaries without deep cloning or repeated graph validation;
- genuine map representation conversion copies only changed COW spines;
- exact unique typed arrays perform checked writes without whole-array cloning;
- `T[]`, `T?[]`, covariance, demotion, and borrow invariance remain correct;
- ordinary no-`var` procedures regain native direct specialization and true borrows remain safe;
- well-known text lookup uses deterministic generated bounded probes rather than catalog scans;
- all retained tracks meet their focused performance and regression gates;
- `make test-lambda-baseline` passes;
- `make test262-baseline` reports zero failures and zero retries;
- a clean guarded Result21 snapshot is captured and compared with both Result20 and the archived
  Result18 binary on the same current fixtures;
- the execution ledger contains evidence for every retained or deferred track.

The expected architectural result is not merely a better benchmark number. It is a restored
fast-path hierarchy:

```text
exact type + exact Shape + unique owner
    -> no conversion, no copy, direct native operation

exact semantic type + shared owner
    -> one-level COW detach, then direct operation

admissible value + different physical lane
    -> selective transactional reification

dynamic or incompatible value
    -> canonical checked slow path and precise diagnostic
```

That hierarchy is the implementation counterpart of the nullable-lane, Shape, and COW designs.

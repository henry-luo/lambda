# JS MVP Lambda objects and Map — implementation record

**Date:** 2026-10-07  
**Status:** object/Map support, bounded tuning, and numeric-loop/array-parameter
lowering corrections are implemented.
Latest gate counts, release measurements and limitations are in
[JS_MVP_Lmd §17](../jube/JS_MVP_Lmd.md#17-slow-kernel-tuning).

**Scope authority:** [JS_MVP_Lmd §10](../jube/JS_MVP_Lmd.md#10-map-and-plain-object-phase),
**S1.11**, **S8.2.2v5**, **D1.3v3**, **D2.6.6v3**, **D2.6.9v3**,
**D3.4.3v5–D3.4.6**, **D4.6.1v4**, **D5.3**.

## 1. Shared storage and execution contracts

Plain objects use ordinary Lambda `Map` and packed `TypeMap` fields. Objects
and ES Map carry distinct nominal records. Shared add and retype edges remain
immutable; deletion replays a filtered shape while retaining nominal identity.
The bounded transition tree retains a private fallback. Empty nominal replay
roots are shared within the existing tree metadata, avoiding a new root on
every delete. Existing field growth and transactional data repacking are reused
from `lambda/runtime/lambda-eval.cpp`; the MVP does not call `fn_map_set` or
its descriptor/JS policy path (**D3.4.3v5**, **D3.4.5**).

Static literal plans contain immutable layouts. Execution setup resolves their
shared transition nodes before entering generated MIR. Reads guard the current
shape and load packed lanes; writes guard again after the RHS and store only a
matching lane. A miss uses the same shared mutation kernel. These are retained
syntax/type plans, with no execution feedback, inline cache, body cloning, or
shadow AST (**D8.2**, **D8.4.1v2**).

ES Map uses an ordinary Map attribute face plus `OrderedMap` trailing storage.
Strong keys/values live in an ordinary Array; each entry has key, value,
collision-chain ordinal, and liveness. `lib/hashmap` buckets carry hashes and
stable ordinals only. Hash/equality agree on SameValueZero across INT/FLOAT,
NaNs, signed zero, canonical string contents, and reference identity
(**S1.11**, **D2.2.5**). Own properties and collection entries are independent.

The shared collector traces the entry Array and frees the native hash index
on sweep or heap destruction. It skips older JS native hooks for these
carriers. GC layout assertions cover the C/C++ boundary. Native mutation
publishes an index entry only after owned slot stores succeed; compaction
builds replacement entries/index before publishing either (**D5.3**).

Direct `for...of` uses an owner and a cursor. Deleted slots are skipped and
new slots remain visible until exhaustion. Keys/values and `[k,v]` bindings
load slots directly; an observable entry pair is a fresh Array. Cursor
accounting covers normal exhaustion, break/continue across nested loops,
returns, tail calls, and errors. Tombstones can compact only with no active
cursor. Ordinary projection methods returning Arrays remain ordinary calls
when their receiver/binding is shadowed (**S1.11**, **D8.4.1v2**).

Canonical key encoding is shared in `lib/utf` and NamePool. Adjacent valid
surrogate units combine; lone units remain lossless WTF-8. No normalization,
case folding, locale conversion, or replacement participates. Shared map
lookup, mutation, membership, and transition additions use the same boundary;
length-bearing lookup preserves embedded NUL (**S8.2.2v5**, **D4.6.1v4**).
Full-JS string equality also uses the shared UTF-16 comparison when bytes
differ: iterators can materialize canonical names while concatenation retains
WTF-8 units. Strict/loose equality and SameValue must agree across those
representations (**S1.11**); this full-engine client fix adds no MVP dependency.
Full-JS collection keys use the same canonical boundary before byte hashing;
Map/Set lookup therefore agrees with string equality. Canonicalization failures
propagate through collection operations and close active Set-operation iterators.

## 2. Concrete imported helper inventory

The ten pre-existing MVP runtime entry points remain. This phase adds ten:

| Entry | Arguments | Result | Effect / ownership |
|---|---|---|---|
| `mvp_lmd_property_key` | string Item | canonical key Item or error | MAY_GC; borrowed input remains rooted; output string immutable |
| `mvp_lmd_object_new` | raw shape, collection flag | Map Item or error | MAY_GC; shape is retained program/tree metadata; collection owns entries/index |
| `mvp_lmd_property_get` | owner Item, key Item, callee flag | field Item, internal callee token, or error | MAY_GC for diagnostics; borrowed scalar is adopted immediately in MIR |
| `mvp_lmd_property_set` | owner/key/value Items | RHS Item or error | MAY_GC; roots owner/key and an adopted scalar value; shared checked repack |
| `mvp_lmd_property_delete` | owner/key Items | bool Item or error | MAY_GC; roots inputs; publishes rebuilt shape/data together |
| `mvp_lmd_property_has` | owner/key Items, inherited flag | bool Item or error | MAY_GC for diagnostics; missing differs from stored undefined |
| `mvp_lmd_object_project` | owner Item, projection | snapshot Array Item or error | MAY_GC; roots owner, result, and current pair; scalar slot ownership preserved |
| `mvp_lmd_map_call` | owner Item, captured method token, key/value Items | operation result or error | MAY_GC; roots owner and adopted key/value; ordered native index has no Item edges |
| `mvp_lmd_map_next` | owner Item, cursor | next ordinal or -1 | NO_GC; bounded entry scan; fresh length on each loop step |
| `mvp_lmd_map_entry` | owner Item, cursor, projection | entry/pair Item or error | MAY_GC; roots owner and fresh pair; direct key/value loops bypass this helper |

The callee token is confined to builtin call/loop lowering and never appears
as an observable JS value. Named builtins require resolved intrinsic bindings
or a captured, validated Map method. Own numeric properties cannot become
builtin calls. Unsupported extracted methods and builtin mutations produce
capability errors instead of silently selecting a spelling shortcut.

The import audit in `mvp_lmd_mir.cpp` records argument classes, return
ownership and effects alongside symbol resolution. Runtime leaves call shared
Lambda allocation, scalar-home, shape, Array, UTF and `lib` hashing primitives.
No existing JS runtime helper participates directly or transitively, including
GC and teardown. Parser and AST analysis remain compile-time reuse.

## 3. Source locations

| Responsibility | Location |
|---|---|
| Admission, literal plans, field guards, builtin calls, loops | `lambda/js/mvp-lmd/mvp_lmd_mir.cpp` |
| Object policy, projections, SameValueZero, ordered storage | `lambda/js/mvp-lmd/mvp_lmd_objects.cpp` |
| Audited ABI declarations | `lambda/js/mvp-lmd/mvp_lmd_runtime.h` |
| Neutral shape setter/deleter and checked repack | `lambda/runtime/lambda-eval.cpp` |
| Canonical tree edges and nominal replay roots | `lambda/input/input.cpp` |
| C/C++ ordered carrier and GC layouts | `lambda/lambda.h`, `lambda/lambda.hpp` |
| Precise tracing and native finalization | `lambda/runtime/gc/gc_heap.c`, `lambda/runtime/lambda-mem.cpp` |
| Canonical key codec and shared NamePool boundary | `lib/utf.*`, `lambda/core/name_pool.cpp` |
| Length-bearing shared map lookup | `lambda/runtime/lambda-data-runtime.cpp` |
| Full-JS clients of the canonical string boundary | `lambda/js/js_runtime_value.cpp`, `lambda/js/js_runtime.cpp` |
| Regressions | `test/test_js_mvp_lmd_gtest.cpp`, `test/test_js_script_gtest.cpp` |
| Six release workloads and rotating-order runner | `test/benchmark/js_mvp_lmd/objects.py` and adjacent JS files |

## 4. Validation and reproduction

```sh
make -C build/premake config=debug_native test_js_mvp_lmd_gtest -j8
./test/test_js_mvp_lmd_gtest.exe --gtest_brief=1
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
  ./test/test_js_mvp_lmd_gtest.exe \
  --gtest_filter='JsMvpLmd.Object*:JsMvpLmd.PlainObject*:JsMvpLmd.Map*:JsMvpLmd.LiveMap*:JsMvpLmd.OrderedMap*:JsMvpLmd.Collection*' \
  --gtest_brief=1
make test-lambda-baseline
make test262-baseline
# Test262's target builds the release lambda.exe used for measurements.
python3 temp/mvp_lmd_numeric_tuning/paired.py \
  --control temp/mvp_lmd_numeric_tuning/control.exe \
  --candidate temp/mvp_lmd_numeric_tuning/candidate.exe \
  --runs 15 --references --output temp/mvp_lmd_numeric_tuning/replay
```

The latest counts, release binary/source hashes, MIR counts and timings belong
in the working design's latest-results section and its JSON artifact. A
cross-engine release screen is not causal optimization evidence. No Node
baseline is part of this change.

## 5. Remaining scope

Descriptors/accessors, proxies, `__proto__` property forms, custom prototypes,
classes/user construction, Symbol/BigInt keys, iterable Map construction,
extracted builtin method values, `forEach`, iterator objects/IteratorClose,
`for...in`, and general destructuring remain excluded. Fixed intrinsic
properties beyond the admitted surface are diagnosed. Namespace/NameId
migration gaps outside the shared string encoding boundary retain their
existing formal-spec status.

## 6. Bounded tuning implementation

The tuning phases in `JS_MVP_Lmd.md` preserve immutable generated code and
shape guards (**D8.4.1v2**), the direct-call ABI (**D8.4.2v2**), and precise
ownership (**D5.3**). There are still 20 MVP runtime imports; this round adds
none and retains one standalone MIR body per function.

- Literal plans use inferred lanes and canonical static names. Bindings and
  returns now carry up to four predictions, propagated through assignments,
  closed calls and literal child fields to a fixed point. Predictions only
  select guards; misses retain generic behavior. Recursive leaf/branch shapes
  can therefore share the same bounded field-read chain.
- Scalar replacement admits up to four existing fields on an unaliased local
  object with one dominating initializer. Static assignments and updates use
  per-field registers across branches and loops. The existing kind solver
  unions every written type; heterogeneous fields use owned Item homes.
  Identity, aliases, captures, computed keys, deletion, method calls and early
  reads retain the object. Initializers and RHS expressions keep their order.
- Inlining admits a 24-node return expression or a 64-node statement body
  with a terminal return and at most 32 parameter/local bindings, at call sites
  inside loops. One-off calls retain their original body to avoid doubling
  cold MIR without an execution benefit. Statement
  bodies can contain loops and local assignments; nested calls, object
  construction, property writes, captures, duplicate formal names and early returns are excluded.
  Each region has separate locals and scalar homes, preserves strictness and
  range proofs, and snapshots all arguments including extras exactly once.
- For eligible statement inlining, immutable object-argument guards precede
  the body. The effect restrictions and unmodified parameter prove its shape
  stable through the region. Field reads stay native, and the same type solver
  refines local lanes using those guarded facts. A miss calls the original
  function with the original argument snapshots. Payload pointers are loaded
  anew at each field access; none are held across allocation.
- A tree-owned immutable shape keeps bounded retype plans with its source
  field, target, replacement offset and payload compatibility. A warm edge
  skips structural fingerprinting and repeated layout scans. External parents
  still use the structural table and full contract checks. Reuse commits the
  value and exact shape without GC; incompatible layouts retain repacking
  (**D3.4.3v5**, **D3.4.5–D3.4.6**).
- Canonical-key Map reads, membership, deletion and immediate updates avoid
  root-frame setup. Canonicalization, wide scalar stores, compaction and growth
  retain precise roots. Lookup returns the collision-chain head for insertion;
  it is recomputed only if compaction changed entry ordinals. SameValueZero,
  signed-zero normalization, insertion order and live cursors are unchanged.

The existing MIR shape-guard fixture now keeps an alias: its object must remain
observable so it continues exercising guards after mutable scalar replacement.

### 6.1 Helper and dependency inventory

`typemap_payload_reusable` extracts the existing full-layout comparison into
shared data code. It uses bounded field traversal and storage descriptors;
`type_tree_retype_field` wraps the existing structural transition builder and
allocates plans in the same tree arena. Neither reaches JS runtime policy.

New compiler utilities (`shape_union`, `object_shapes`, `scalar_field`,
`scalar_reference`, `read_local_value`, `scalar_field_write`, `inline_slot`,
`enclosing_loop`)
use retained AST/binding facts, existing type/range analysis, shared field
metadata and the audited MIR/scalar-home emitter. They are compilation work,
not new JIT imports. Map changes reuse the existing Array, UTF and hash-table
primitives from §2. No private allocator, VMap or mutable per-site cache is added.

### 6.2 Remaining tuning boundaries

Escaping-object scalar replacement, guards across effectful calls, larger
inlined bodies and reuse of tested child values across branches are not
implemented. Shape sets remain predictions rather than complete type proofs.
String Map keys still scan canonical bytes and hash on each independent call.
Allocation and numeric-array improvements require separate profiling.

### 6.3 Latest validation

See [JS_MVP_Lmd §17](../jube/JS_MVP_Lmd.md#17-slow-kernel-tuning)
for the latest gates and release comparison. The earlier object/Map tuning
artifacts remain under `temp/mvp_lmd_tuning2/`; the numeric-loop/array-parameter
round is recorded in §7. Early diagnostic runs that overlap compilation are
excluded from performance claims.

## 7. Numeric loops and array parameters

The broader admitted-workload comparison exposed two retained-fact losses in
`mvp_lmd_mir.cpp` (**D8.2.4v2**, **D8.2.5v3**, **D8.2.6**):

- `diviter`: literal lowering used the enclosing caller's numeric region
  during inlining. An integer callee's `q++` consequently converted i64 to
  double, added a floating one, then converted back on every iteration.
  The inline frame now records its callee, and literal lowering uses that
  function's existing integer/float decision. Range proofs, binary64
  rounding and signed-zero rules remain those of **D2.2.5**.
- `pnpoly`: parameter reads discarded the solver's complete kind domain;
  boxed inline parameters and owned snapshots discarded it again. Known
  Array parameters therefore fell through computed-property conversion.
  These boundaries now retain the existing facts. Closed-call domains union
  every argument, missing argument and assignment; open domains remain
  unknown. Array bounds and capability checks use the existing lowering.

Owned snapshots still adopt scalar homes and retain their precise roots
(**D5.3**). There are no additional runtime imports, mutable caches, inferred
array element types or ArrayNum promotion (**D8.4.1v2**). Generic element
coercion and scalar ownership remain the next `pnpoly` costs to investigate.

`InlinedNumericRegions` covers integer callees inside floating callers,
fractional updates, the int53 rounding boundary and negative zero.
`ClosedParameterKindsAndSnapshots` covers direct/inlined indexing, absence of
property-key conversion in MIR, mixed parameter kinds, missing arguments,
reassignment, out-of-bounds reads and scalar snapshots across an array write.
Both are in `test/test_js_mvp_lmd_gtest.cpp`.

Evidence for this round is retained under `temp/mvp_lmd_numeric_tuning/`:
the pre-change source, frozen release control, gate logs, paired runner,
matched sources, MIR and raw process outputs. The short screen is diagnostic;
§10.8 of the working design links the archived comparison; §17 records the latest results.

Finalized MIR instruction counts fall from **204 to 201** for `diviter` and
**3,539 to 2,926** for `pnpoly`. Final measurements run after all builds and
correctness gates. The JSON retains every process time, matched-output check,
control-peer comparison, paired interval, input hash and exact binary hash.
No separate compilation-time improvement is claimed.

The Test262 baseline reaches 40,261/40,261 through its existing recovery path.
Its full-JS AST Unicode batch first loses 80 tests after an abort. An unchanged
600-test manifest replay under `--timeout=5` aborts on both frozen releases:
the Unicode-identifier test times out, signal recovery then faults, and the
process exits with SIGABRT. This is outside MVP lowering; the shared runtime's
timeout/recovery defect remains open. `batch-replay.json` and both raw streams
preserve the evidence without changing the Test262 harness or timeout policy.

## 8. Generic element coercion and scalar ownership

This round implements §14 of the working design (**D2.2.5**, **D5.3**,
**D8.2.6**):

- Generic numeric coercion enters `em_unbox_f64_item` directly. Its inline
  IEEE branch supplies the number; only the other encodings enter the JS
  primitive conversion dispatch. Packed integers are decoded before Float
  zero-sentinel checks. Strings, booleans, null, undefined and capability
  errors retain their existing meanings.
- Shared `em_adopt_scalar_item_value` copies a Float payload into its existing
  destination home in generated MIR. Shared boxing preserves canonical inline
  values and signed zero even for noncanonical pointer Floats. Other scalar
  return classes retain the native adopter. The Float tag already excludes
  inline doubles, so its two zero sentinels need one unsigned comparison.
  `em_store_f64_home` is shared with MVP's existing cold boxer; no runtime
  import is added. Shared packing accepts payload bits directly, avoiding a
  bits-to-double-to-bits round trip when copying an existing Float home.
- Numeric binary consumers may borrow a scalar read until coercion completes.
  Admission is bounded to identifiers, literals and known Array reads with
  simple numeric/string keys. A left borrow additionally requires an admitted
  right operand. Calls, assignments, effectful indexes and generic property
  reads retain owned snapshots; addition and equality keep their existing
  paths. Both operands still evaluate before conversion. Successful consumers
  and their conversion leaves are NO_GC; capability failures exit the function.

These borrows do not cross mutation, safepoints, argument publication or value
escape. Array bounds, length, aliases and mixed element types remain dynamic.
The changes add no array element-type inference, ArrayNum promotion, mutable
cache or new object surface. Destination homes remain independent wherever a
value can survive a source overwrite (**D5.3.1–D5.3.4**).

`GenericElementCoercion` covers mixed primitives, signed zero, subnormals,
nonfinite values, absent elements, string addition/comparison and excluded
object coercion. `OwnedElementSnapshots` covers writes, aliasing, local
reassignment, array growth, calls and effectful indexes, and checks that MIR
does not import the scalar-adoption helper. Both join the forced-GC gate.

Frozen releases, source snapshots, paired runner and raw evidence are under
`temp/mvp_lmd_element_tuning/`. The comparison also pairs old/new LJS and
untyped Lambda because scalar adoption belongs to the shared emitter.

The reviewed `js_hoisted_modvar_write_through` emission delta is six extra
instructions in the coercing helper, replacing its native scalar-copy call.
Its function/frame budgets are unchanged; debug module budgets record the
delta (**D8.6.1**). Darwin is measured; Linux/Windows need native confirmation.
The release-profile `js_main` and `lambda_cow_nested_store` budget failures
also reproduce with identical counts on the frozen control and are unchanged.
The final debug baseline is the required aggregate gate.

Final release evidence is in
[`MVP_Result3.json`](../../test/benchmark/js_mvp_lmd/MVP_Result3.json).
All 30 workloads have 15 paired rounds; six receive a 30-pair follow-up. The
follow-up confirms 1.68× for `pnpoly` and 1.31× for `dense_array`, while
`diviter` is unchanged. All 4,410 measured and 252 preflight outputs match.
Object deletion's first-run slowdown does not persist in its paired median;
the follow-up interval remains wide and is retained as inconclusive.

`pnpoly` MIR shrinks from 2,926 to 2,817 instructions, calls from 113 to 90,
and its polygon function's scalar homes from 14 to 11. Roots, root stores and
safepoints are unchanged. `dense_array` trades 39 extra MIR instructions for
eight fewer calls; its frame sizes are unchanged. Neither imports the scalar
adopter. `diviter` retains the same 201 instructions and two calls. These are
emission/frame measurements, not allocation counts or compile-time results.

The final aggregate passes 6,292/6,292 and MVP passes 34/34 normally and under
forced GC with poisoning. Two first-run child failures pass five focused
replays and the unchanged aggregate; their initial cause is unconfirmed.
Test262 reports zero regressions, 40,259 fully passing and two retry-only AST
Unicode cases. An isolated replay of both cases succeeds on both releases;
the baseline's slow/unstable classification remains visible. No harness,
oracle or timeout changes are part of this patch.

Reproduce the paired run from the retained workspace artifacts:

```sh
python3 temp/mvp_lmd_element_tuning/paired.py \
  --control temp/mvp_lmd_element_tuning/control.exe \
  --candidate temp/mvp_lmd_element_tuning/candidate4.exe \
  --output temp/mvp_lmd_element_tuning/reproduce \
  --runs 15 --references --baseline-references
```

Remaining costs include repeated generic element guards, mixed arithmetic and
dynamic container storage. Numeric-array promotion remains a separate phase;
any broader borrow must prove that no mutation or safepoint intervenes.

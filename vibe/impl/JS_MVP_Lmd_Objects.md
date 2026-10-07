# JS MVP Lambda objects and Map — implementation record

**Date:** 2026-10-07  
**Status:** implemented; MVP 30/30, forced-GC object/Map groups 10/10, and
Lambda/input baseline 6,286/6,286. Full-JS Test262 has zero semantic regressions
with two retry-only Unicode cases; this is not an all-fully-passing result.
Six checked release workloads and the remaining MIR/performance limits are
recorded in [JS_MVP_Lmd §10.8](../jube/JS_MVP_Lmd.md#108-latest-validation-and-release-evidence--2026-10-07).  
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
python3 test/benchmark/js_mvp_lmd/objects.py --runs 5
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

## 5. Bounded tuning implementation

The tuning phase in `JS_MVP_Lmd.md` keeps **D8.4.1v2** immutable guards and
**D5.3** precise ownership. It adds no JIT runtime imports and uses the existing
AST index, type/range solver, field emitter, direct-call metadata, scalar homes
and shared transition storage.

- Literal planning accepts inferred numeric expressions. Static identifier keys
  are canonicalized by the shared name pool during compilation; field stores
  reuse the guarded writer. Only the first store into a fresh owner omits its
  shape guard.
- Each binding/return carries one immutable shape prediction. Aliases, closed
  parameters and recursive return/call edges propagate it to a fixed point;
  other layouts use the existing slow path. Closed calls pass individual
  operands independently of their result representation (**D8.4.2v2**), with
  boxed arguments described as precise Items.
- Scalar replacement admits at most four fields, a single dominating
  initializer, and only static reads of existing fields. Identity uses, aliases,
  captures, mutation, method calls and early reads retain allocation. Initializers
  execute once in order. Their proven facts feed the existing solver again so
  arithmetic can retain native lanes. Lambda's contract-driven record analysis
  is not applicable to mutable JS identity; its shared AST index, shape storage
  and MIR/rooting substrate are reused.
- Inlining accepts a closed function with one returned expression and at most
  24 AST nodes. Calls and writes inside that expression are excluded; arguments,
  including discarded extras, are evaluated before parameter substitution.
- Heap payloads remain in place for transitions whose surviving fields retain
  their offsets/widths and whose replacement has a simple nonallocating store.
  The complete layout is checked before publishing the new shape; repacking
  remains the fallback (**D3.4.5–D3.4.6**). Existing add transitions already reuse
  spare payload capacity.
- Named Map methods use an empty-attribute-shape guard and the shared method
  spelling classifier. Selection occurs before arguments run. Numeric hashing
  keeps normalized NaN/zero bits; immediate entry writes use the existing array
  store's nonallocating path. Wide numeric homes, canonical strings and live
  iteration retain their existing ownership rules.

General polymorphic guard chains, loop-body inlining and scalar replacement of
escaping objects are outside these bounded optimizations. No new private
allocator, mutable cache, VMap path or JS semantic feature is introduced.

### 5.1 Latest validation

The current sources pass MVP **30/30**, forced-GC/poison object and Map groups
**10/10**, and the shared Lambda/input baseline **6,286/6,286**. Test262 reports
zero semantic failures/regressions: **40,259 fully passed + 2 retry-only** Unicode
10 identifier cases. The focused GTest configuration is `debug_native`;
performance binaries are release builds.

The final release comparison uses 15 alternating pairs, a control peer in every
round, and Node; all **16 oracles / 960 measured outputs** pass. Numeric, array,
call and string controls are included. High host load leaves small changes
unresolved, including retyping; transition lookup is a remaining profiling
target. Exact binary/source hashes, paired intervals, control/control ratios,
and gate-log hashes are in
[`tuning_mir_20261007.json`](../../test/benchmark/js_mvp_lmd/tuning_mir_20261007.json).
Raw evidence and the frozen reproducer are under `temp/mvp_lmd_tuning/confirm/`.
The earlier `screen/` overlapped build work and is excluded from final claims.

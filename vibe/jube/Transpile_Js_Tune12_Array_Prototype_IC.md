# Transpile JS Tune12 Proposal: Array Named Properties and Prototype Constructor Shapes

Date: 2026-06-25
Status: P1 array named-property IC, P1b array companion-map store
stabilization, P2 function-constructor shape cache, P2b prototype inheritance
composition, P3 name-id metadata, HashMap sparse-array P1/P2,
Navier-Stokes dense array get fast path, and Cube3D append/store-path
profiling implemented; GC sweep ownership classification tuned; P4-P5 remain
proposals

Primary sources:

- Current benchmark summary: `test/benchmark/Overall_Result7.md`
- Tune11 IC implementation notes: `vibe/jube/Transpile_Js_Tune11_Callsite_Cache.md`
- Property IC runtime: `lambda/js/js_runtime.cpp`, `lambda/js/js_runtime.h`
- MIR member lowering: `lambda/js/js_mir_expression_lowering.cpp`
- Class/function constructor lowering: `lambda/js/js_mir_statement_lowering.cpp`,
  `lambda/js/js_mir_module_batch_lowering.cpp`
- Profile artifacts from the Tune12 planning pass:
  - `temp/profile_cube3d.tsv`
  - `temp/profile_awfy_deltablue.tsv`
  - `temp/profile_jetstream_deltablue.tsv`
- Cube3D append/store and GC follow-up artifacts:
  - `temp/profile_jetstream_cube3d_inline_poc.tsv`
  - `temp/profile_jetstream_cube3d_expand_gc.tsv`
  - `temp/profile_jetstream_cube3d_gc_phases.tsv`
  - `temp/profile_jetstream_cube3d_gc_sweep_counts.tsv`
  - `temp/profile_jetstream_cube3d_gc_sweep_flag_no_poc.tsv`
- Navier-Stokes dense array get follow-up artifacts:
  - `temp/profile_jetstream_navier_stokes.tsv`
  - `temp/profile_jetstream_navier_stokes_dense_get_fast.tsv`

## Goal

Tune11 added per-callsite load and store ICs for compiled fixed-name property
sites such as `a.b` and `a.b = v`. Those ICs work well when the receiver is a
descriptor-free plain map with a stable shared `TypeMap*`.

Tune12 should target the next measured bottleneck: hot property sites where the
current IC cannot participate or becomes megamorphic quickly.

The top benchmark gaps in `Overall_Result7.md` are:

1. `jetstream/cube3d`
2. `awfy/deltablue`
3. `jetstream/deltablue`

The Tune12 profile pass shows that these are not all the same problem:

- `jetstream/cube3d` is dominated by named properties on arrays used as
  object-like records.
- `awfy/deltablue` already gets strong store-IC behavior from ES class
  constructor shaping, but still spends heavily in property reads and calls.
- `jetstream/deltablue` uses prototype-style constructors and hand-written
  inheritance, causing many equivalent but non-shared shapes and therefore
  megamorphic IC sites.

Tune12 should keep each candidate separately measurable and separately
disableable where practical.

## Implementation Update: P1 Array Named-Property IC

Implemented on 2026-06-25:

- extended fixed-name load/store IC entries with `receiver_kind`, separating
  normal map receivers from array companion-map receivers;
- taught `js_property_access_named_ic()` and `js_property_set_named_ic()` to
  resolve eligible array receivers through `arr->extra` for non-index,
  non-`length` property names;
- kept installs conservative: the ordinary semantic get/set path still runs
  before a store IC install, and the IC only caches descriptor-free own data
  slots on the array companion `MAP_KIND_ARRAY_PROPS` map;
- preserved array exotic semantics by rejecting numeric names and `length`;
- added `LAMBDA_JS_ARRAY_NAMED_IC=0` as the per-phase disable flag.

Verification:

- `make build` passed.
- `./lambda.exe js temp/tune12_array_named_ic_smoke.js --no-log` passed.
- `LAMBDA_JS_ARRAY_NAMED_IC=0 ./lambda.exe js temp/tune12_array_named_ic_smoke.js --no-log`
  passed.
- `make test262-baseline` passed: 40,261 / 40,261 fully passing, 0
  regressions, 2,628 skipped.
- `make build-release-profile` passed.
- `git diff --check` passed.

Profile A/B on `temp/profile_jetstream_cube3d.js` with
`JS_EXEC_PROFILE=2`:

```text
enabled:  JS_EXEC_PROFILE_OUT=temp/tune12_profile_cube3d.tsv
disabled: JS_EXEC_PROFILE_OUT=temp/tune12_profile_cube3d_array_ic_off.tsv
```

Aggregate IC counters:

```text
                       enabled      disabled
load_ic_miss              250        161618
load_ic_hit_poly        44019          1878
store_ic_miss             134         20052
store_ic_hit_poly        5651           699
```

Hot array-property site deltas:

```text
site                    disabled miss_not_map   enabled miss_not_map   enabled hits/install
Q.NumPx@323:27          43232                   0                      hit_poly=10444 install=1+3
Q.LinePixels@323:41     43200                   0                      hit_poly=10437 install=1+3
Q.LastPx@65:15          14816                   0                      hit_poly=3696  install=1+3
Q.LastPx@76:7 store     14816                   0                      hit_poly=3697  install=1+3
Q.Line@238:7 store       1664                   0                      hit_poly=412   install=1+3
```

The remaining `megamorphic` counts at these sites come from the current
four-entry polymorphic limit after array companion-map participation starts;
they are not `miss_not_map` rejections.

## Implementation Update: P1b Array Companion-Map Store Stabilization

Implemented on 2026-06-25:

- added an in-place same-size write path for existing own data slots in array
  companion maps, keeping stable companion-map shapes for updates such as
  `Q.LastPx = value`;
- preserved writable-property checks before the in-place update and falls back
  to the generic property setter when the value cannot fit the existing field
  slot;
- allowed direct dense writes to existing array elements even when the array has
  a pure named-property companion map;
- added `TypeMap::has_array_index_shape` for `MAP_KIND_ARRAY_PROPS`, so dense
  writes only probe the companion map for numeric override slots when such a
  numeric companion shape has ever been added;
- made the in-place companion write defer to the generic descriptor-aware setter
  while `Object.defineProperty` is internally replacing a non-writable data slot
  with accessor storage;
- tried widening the load/store IC polymorphic limit from 4 to 8, which removed
  Cube3D megamorphic IC counters in the profile build but did not improve
  release timing, so that change was not kept.

Verification:

- `make build` passed.
- `./lambda.exe js temp/tune12_array_named_ic_smoke.js --no-log` passed.
- `./lambda.exe js temp/tune12_func_ctor_shape_smoke.js --no-log` passed.
- `./lambda.exe js temp/tune12_func_ctor_inheritance_smoke.js --no-log` passed.
- `./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/tune12_defineproperty_regressions.txt --run-async --async-list=test/js262/test262_baseline.txt --jobs=1`
  passed for the two `Object.defineProperty` accessor-conversion regressions
  caught during the first full gate attempt.
- `make build-release-profile` passed.
- `make build-release` passed.
- `make test262-baseline` passed: 40,261 / 40,261 fully passing, 0
  regressions, 2,628 skipped.

Profile snapshots on `temp/profile_jetstream_cube3d.js` with
`JS_EXEC_PROFILE=2`:

```text
before P1b current snapshot:
property_set calls=378825 self_ms=69925.369
load_ic_megamorphic=118400
store_ic_megamorphic=15946

after same-size companion slot update:
property_set calls=358873 self_ms=69644.525
load_ic_megamorphic=83312
store_ic_megamorphic=10956

after dense-extra fast path plus has_array_index_shape:
property_set calls=358873 self_ms=72596.228
load_ic_megamorphic=83312
store_ic_megamorphic=10956
```

The instrumented profile still attributes almost all Cube3D runtime to
`property_set`, so the next useful slice should continue inside the generic
store helper rather than increasing IC width.

Release one-shot timing:

```text
before has_array_index_shape guard: jetstream/cube3d 77.86s
after P1b store-path slice:        jetstream/cube3d 72.68s
```

## Implementation Update: P2 Function Constructor Shape Cache

Implemented on 2026-06-25:

- added a per-collected-function constructor shape cache slot;
- taught direct `new Foo(...)` lowering for stable function declarations with
  collected `this.name = ...` metadata to allocate through
  `js_constructor_create_object_shaped_cached()`;
- added `LAMBDA_JS_FUNC_CTOR_SHAPE=0` to force the generic constructor path;
- kept the path conservative by rejecting reassigned functions, class methods,
  arrow functions, variable-bound function expressions, dynamic callees, and
  constructors without unconditional top-level `this.name = ...` metadata.

The first P2 slice intentionally does not yet compose static super-constructor
metadata for `Ctor.superConstructor.call(this, ...)`. That is the remaining
follow-up for the `this.direction`/`this.v2` sites below.

Verification:

- `make build` passed.
- `./lambda.exe js temp/tune12_func_ctor_shape_smoke.js --no-log` passed.
- `LAMBDA_JS_FUNC_CTOR_SHAPE=0 ./lambda.exe js temp/tune12_func_ctor_shape_smoke.js --no-log`
  passed.
- `make build-release-profile` passed.
- `./lambda-profile.exe js test/benchmark/jetstream/deltablue.js --no-log`
  passed.
- `./lambda-profile.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log`
  passed with `DeltaBlue: PASS`.
- `make test262-baseline` passed: 40,261 / 40,261 fully passing, 0
  regressions, 2,628 skipped.
- `git diff --check` passed.

Profile A/B on `temp/profile_jetstream_deltablue.js` with
`JS_EXEC_PROFILE=2`:

```text
enabled:  JS_EXEC_PROFILE_OUT=temp/tune12_profile_jetstream_deltablue_p2.tsv
disabled: JS_EXEC_PROFILE_OUT=temp/tune12_profile_jetstream_deltablue_p2_off.tsv
```

Aggregate IC counters:

```text
                       enabled      disabled
load_ic_hit_mono      2372415        640543
load_ic_megamorphic   1151828       2803686
store_ic_hit_mono      334480           442
store_ic_megamorphic    12024        344466
```

Hot site deltas:

```text
site                    disabled behavior          enabled behavior
this.elms@70:10 load    megamorphic=360532         hit_mono=360539
expr.value@525:3 store  megamorphic=203996         hit_mono=203999
out.mark@187:3 store    megamorphic=10136          hit_mono=10139
this.direction@413:11   megamorphic=274320         still megamorphic=274320
this.v2@413:50          megamorphic=263820         still megamorphic=263820
```

The large store-IC improvement confirms that declaration-backed function
constructors now share constructor shapes. The remaining two load sites are
prototype-inheritance composition work, not a failure to use the P2 cache.

## Implementation Update: P2b Prototype Inheritance Composition and P3 Name IDs

Implemented on 2026-06-25:

- extended function-constructor scanning to identify conservative
  `Ctor.superConstructor.call(this, ...)`, `Ctor.super_.call(this, ...)`, and
  direct parent `.call(this, ...)` patterns;
- required `Ctor.inheritsFrom(Base)` to agree with the static super-call target
  before composing inherited constructor fields;
- composed base-first function-constructor field metadata before MIR emission,
  so the existing cached `new Ctor(...)` path allocates the final shape;
- marked unknown `.call(this, ...)` patterns as dynamic and forced the generic
  constructor path instead of using an own-only partial shape;
- added `ShapeEntry::name_id` plus load/store IC `name_id` metadata, using the
  compact ID to reject mismatches while retaining pointer/length/string
  comparison as the authoritative equality check.

Follow-up implemented after the initial P3 slice:

- added `typemap_hash_lookup_by_id()` and `typemap_shape_name_equals_id()` so
  callers that already have a property `name_id` can avoid recomputing the
  FNV/name fingerprint during TypeMap lookup;
- reordered named load/store IC key checks so the stable fixed-name pointer hit
  path returns before hashing the name, while still using `name_id` and string
  comparison for non-identical pointers and collision safety;
- changed named load/store IC install probes to look up the `ShapeEntry`
  directly from the resolved receiver `Map` with the precomputed `name_id`;
- added `TypeMapTransition::name_id`, using it as an early reject for shared
  constructor/transition shape growth before falling back to pointer/length and
  string comparison;
- eagerly populates `ShapeEntry::name_id` when shape entries are created or
  cloned in the input writer, shape pool, JS pre-shaped object allocator, and
  type-change rebuild path.

Name-id fix summary:

- `name_id` is now treated as a cached comparison accelerator, not as the sole
  authority for property identity. Every externally visible equality decision
  still falls back to pointer/length/string comparison so hash collisions remain
  safe.
- Hot fixed-name IC hit checks avoid computing `name_id` when the compiled
  property site reuses the same name pointer. This removes avoidable FNV work
  from the common monomorphic hit path.
- IC install/build code now reuses a single `name_id` through receiver TypeMap
  lookup instead of redispatching through object property lookup and hashing
  the property name again.
- Shape transition lookup stores the added property's `name_id` and uses it as
  an early reject before byte comparison, which keeps constructor/shared-shape
  growth from doing unnecessary string checks.
- Shape-entry allocation, cloning, pre-shaped JS object allocation, and
  type-change rebuild all preserve or eagerly compute `name_id`, so later IC
  install and transition paths do not pay lazy initialization costs.

Verification:

- `make build` passed.
- `./lambda.exe js temp/tune12_func_ctor_shape_smoke.js --no-log` passed.
- `LAMBDA_JS_FUNC_CTOR_SHAPE=0 ./lambda.exe js temp/tune12_func_ctor_shape_smoke.js --no-log`
  passed.
- `./lambda.exe js temp/tune12_func_ctor_inheritance_smoke.js --no-log`
  passed.
- `LAMBDA_JS_FUNC_CTOR_SHAPE=0 ./lambda.exe js temp/tune12_func_ctor_inheritance_smoke.js --no-log`
  passed.
- `./lambda.exe js test/benchmark/jetstream/deltablue.js --no-log` passed.
- `./lambda.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log`
  passed with `DeltaBlue: PASS`.
- `make build-release-profile` passed.
- `make test262-baseline` passed: 40,261 / 40,261 fully passing, 0
  regressions, 2,628 skipped.
- `git diff --check` passed.
- after the name-id follow-up, `make release` passed.
- after the name-id follow-up, the original JetStream hashmap one-shot release
  run was `10.70s` versus the previous local `10.78s`. Treat this as a small
  or noisy improvement; it confirms the fix is safe but does not remove the
  main hashmap bottleneck.
- after the name-id follow-up, `make test262-baseline` passed again: 40,261 /
  40,261 fully passing, 0 regressions, 2,628 skipped.

Profile A/B on `temp/profile_jetstream_deltablue.js` with
`JS_EXEC_PROFILE=2`:

```text
enabled:  JS_EXEC_PROFILE_OUT=temp/tune12_profile_jetstream_deltablue_p2b_p3.tsv
disabled: JS_EXEC_PROFILE_OUT=temp/tune12_profile_jetstream_deltablue_p2b_p3_off.tsv
```

Aggregate IC counters:

```text
                       enabled      disabled
load_ic_hit_mono      2432746        640543
load_ic_hit_poly      1091658         79933
load_ic_megamorphic         0       2803686
store_ic_hit_mono      336876           442
store_ic_hit_poly        9637          1536
store_ic_megamorphic        0        344466
```

Hot site deltas:

```text
site                    disabled behavior          enabled behavior
this.elms@70:10 load    megamorphic=360532         hit_mono=360539
this.direction@413:11   megamorphic=274320         hit_mono=11699 hit_poly=262639
this.v2@413:50          megamorphic=263820         hit_mono=11199 hit_poly=252639
expr.value@525:3 store  megamorphic=203996         hit_mono=203999
out.mark@187:3 store    megamorphic=10136          hit_mono=10139
```

Instrumentation wall time was effectively neutral in the profile run
(`7936.456` enabled vs `7951.168` disabled), so the profile should be read as
hot-path attribution rather than benchmark timing.

## Implementation Update: HashMap Sparse-Array P1/P2

Implemented on 2026-06-25:

- added adaptive promotion from `SparseArrayMap` numeric sparse hash storage
  back to holey dense storage when a normal array becomes dense enough:
  `length <= 262144`, sparse count >= `4096`, and sparse density >= 25%;
- kept promotion conservative: arguments/content arrays are excluded, and
  every sparse entry must be within the current array length before migration;
- migrated sparse entries into a fresh dense item backing, preserved existing
  dense slots, installed the dense backing, and released the sparse numeric
  hash once promotion succeeds;
- added `TypeMap::has_array_index_shape` use on array companion maps so hot
  numeric array get/set paths skip digit-string companion-map probes until a
  numeric descriptor/accessor shape has actually been installed;
- marked numeric companion shapes when descriptor flags/accessors are created
  or updated, preserving array-index semantics for `Object.defineProperty`;
- fixed descriptor conversion safety for accessor-to-data updates across plain
  objects, array/arguments companion maps, and function `properties_map` slots
  by raw-replacing the `JsAccessorPair` slot before clearing
  `JSPD_IS_ACCESSOR`.

Verification:

- `make build` passed.
- `./lambda.exe js temp/tune12_sparse_promote_smoke.js --no-log` passed.
- `./lambda.exe js temp/tune12_array_index_descriptor_smoke.js --no-log`
  passed.
- `./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/tune12_array_descriptor_regressions.txt --run-async --async-list=test/js262/test262_baseline.txt --jobs=1 --js-timeout=60`
  passed: 2 / 2.
- `./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/tune12_remaining_regressions.txt --run-async --async-list=test/js262/test262_baseline.txt --jobs=1 --js-timeout=60`
  passed: 3 / 3.
- `make test262-baseline` passed: 40,261 / 40,261 fully passing, 0
  regressions, 2,628 skipped.
- release benchmark with profiling instrumentation disabled:
  `jetstream/hashmap` median of 3 = `9.47s`.

Result:

- previous local hashmap after the name-id follow-up: `10.70s`;
- current P1/P2 sparse-array result: `9.47s`;
- delta: about 11.5% faster on original JetStream hashmap.

## Implementation Update: Navier-Stokes Dense Array Get Fast Path

Implemented on 2026-06-25:

- increased the JetStream `navier_stokes` JS benchmark load from `32x32` to
  `128x128`, matching the existing checksum path and giving a profile sample
  long enough to diagnose;
- added `js_array_fast_own_dense_get()` as a deliberately narrow runtime fast
  path for `array[int]` reads in `js_property_access()`;
- the fast path returns `arr->items[idx]` directly only for normal Array
  receivers with `idx >= 0`, `idx < length`, `idx < capacity`, a non-hole dense
  slot, and no numeric companion descriptor/accessor for that index;
- arguments/content arrays, holes, sparse entries, numeric accessors,
  descriptor-shadowed indexes, and prototype numeric lookups still fall through
  to the semantic path.

Reason:

- the `128x128` Navier-Stokes profile showed computed dense array reads going
  through `js_property_access()` and then `js_property_get()` because many
  index expressions are arithmetic-derived and lower as boxed property keys;
- using `js_array_get_int()` would still pay companion/prototype/sparse
  semantic checks, so the useful fast path is the plain dense-own read case.

Profile effect:

```text
                                  before        after
release median of 3                222 ms       149 ms
profile timing                  403.394 ms   258.542 ms
property_get calls              2,105,925       1,821
property_get self_ms              106.303       0.789
property_access self_ms            83.078      40.852
```

Remaining hot path:

- generic array stores are now the main Navier-Stokes runtime target:
  `property_set` still sees about `906k` calls, with about `229k` generic
  `top_array` calls in the profiled run;
- `array_set_ta_proto_numeric` and the numeric conversion/prototype preamble
  remain visible inside those generic array stores.

Verification:

- `make build` passed.
- `./lambda.exe js temp/tune12_array_dense_get_fast_smoke.js --no-log`
  passed.
- `./lambda.exe js temp/tune12_array_dense_get_fast_edges.js --no-log`
  passed.
- `make release` passed.
- `make test262-baseline` passed: 40,261 / 40,261 fully passing, 0
  regressions, 2,628 skipped.
- `git diff --check` passed.

## Implementation Update: Cube3D Append/Store Path and GC Bottleneck

Implemented and profiled on 2026-06-25:

- split the dense array store profile branches so append-at-length is tracked
  separately from true gap expansion;
- kept `array_set_expand_dense` as the true gap-fill path and routed
  `idx == length` through `array_set_append_dense`;
- added experimental runtime helpers
  `js_array_set_append_or_dense_int_fast()` and
  `js_array_set_append_or_dense_item_fast()` for append/dense stores, but did
  not keep MIR emission through those helpers after timing showed a regression;
- added an intentionally unsafe inline-MIR POC for computed
  `array[index] = value` stores to measure the ceiling of bypassing the
  property setter call. This POC checks only enough to avoid obvious
  out-of-capacity dense writes and is not a correctness candidate. The active
  POC emission was removed after measurement.

The append/gap split clarified the profile:

```text
before split:
array_set_expand_dense    ~250560 calls

after split:
array_set_append_dense     249736 calls
array_set_expand_dense       1664 calls
```

The runtime-helper emission proved that many hot stores could be intercepted,
but it did not improve release timing:

```text
last release baseline without helper emission:  __TIMING__:73048.933
runtime helper emission:                       __TIMING__:75973.906
runtime helper repeat:                         __TIMING__:84736.769
```

The inline-MIR POC also intercepted a large part of the computed array-store
traffic, but remained effectively flat to slightly worse in release timing:

```text
release inline-MIR POC:       __TIMING__:73419.631
profile inline-MIR POC:       __TIMING__:72729.762

property_set                  165881 calls
array_set_append_dense         65640 calls
array_set_expand_dense          1664 calls
array_push_direct_expand       66945 calls
```

The key finding is that the apparent `array_push_direct_expand` hot path is
mostly where GC is triggered, not where array copying or hole stamping is
expensive. Phase-level profiling showed:

```text
array_push_direct_expand       66945 calls   67720.301 ms
expand_list_heap_alloc         66946 calls   67706.725 ms
heap_data_alloc                66952 calls   67703.766 ms
heap_gc_collect                    1 call    67702.025 ms
gc_collect_total                   1 call    67702.023 ms
gc_sweep                           1 call    67084.194 ms

array_push_expand_stamp_holes  66945 calls       2.050 ms
expand_list_heap_copy            248 calls       0.041 ms
gc_compact_data                    1 call        5.998 ms
```

The sweep-count profile shows why the single GC dominates Cube3D:

```text
gc_collect_start_objects      4450856
gc_sweep_walked_objects       4450856
gc_sweep_alive_objects           5938
gc_sweep_dead_objects         4444918
gc_sweep_object_zone_owned    4420580
gc_collect_slab_count            8838
gc_collect_range_count           8839
gc_sweep_freed_bytes        110323710
```

Diagnosis: `array_push_direct_expand` is not spending meaningful time copying
old array items or stamping newly allocated slots with holes. It triggers
`heap_data_alloc()`, which triggers one very large GC, and that GC spends almost
all of its time in `gc_sweep()`. The likely next tuning target is sweep
ownership classification: the sweep loop checks whether each dead object belongs
to bump blocks, object-zone slabs, or large allocations, and the object-zone
ownership path has to classify millions of objects across thousands of slabs.

This means the next Cube3D tuning slice should move from array store lowering
to GC sweep ownership classification. Further append-store work is unlikely to
pay off until the one-collection `gc_sweep` cost is removed or made much
cheaper.

### GC sweep ownership tuning

Implemented on 2026-06-25. The root cause was not array growth itself. Cube3D
allocated about 4.45M GC-tracked objects, then triggered one collection from the
array/data allocation path. Before tuning, `gc_sweep()` had to decide, for each
dead header, whether the object came from a bump block, an object-zone slab, or a
large allocation. That ownership test was expensive because the object-zone path
had to classify millions of dead objects against thousands of slabs.

The tuning moved ownership classification to allocation time:

- added `GC_FLAG_BUMP` to GC headers and set it at bump-pointer allocation
  time;
- changed `gc_sweep()` to classify ownership from header flags:
  `GC_FLAG_LARGE`, `GC_FLAG_BUMP`, or object-zone by default;
- removed the per-object sweep dependency on `gc_bump_block_owns()` and
  `gc_object_zone_owns()`, avoiding millions of ownership searches across
  thousands of slabs;
- kept the diagnostic profile counters for GC phase and sweep-count attribution
  under `LAMBDA_JS_EXEC_PROFILE`.

The core code shape is:

```text
gc_heap_bump_alloc():
  header->gc_flags = GC_FLAG_BUMP

gc_sweep():
  if GC_FLAG_LARGE: free large allocation
  else if GC_FLAG_BUMP: unlink only; memory is pool-owned
  else: return header to object-zone free list
```

Before tuning:

```text
array_push_direct_expand       66945 calls   67720.301 ms
expand_list_heap_alloc         66946 calls   67706.725 ms
heap_data_alloc                66952 calls   67703.766 ms
heap_gc_collect                    1 call    67702.025 ms
gc_collect_total                   1 call    67702.023 ms
gc_sweep                           1 call    67084.194 ms
```

Cleaned-state profile after removing the unsafe inline-MIR POC:

```text
release Cube3D:               __TIMING__:3752.527
profile Cube3D:               __TIMING__:4082.316

property_set                  358873 calls   1064.084 ms
array_set_append_dense        249736 calls    646.464 ms
array_push_direct_expand       66945 calls    638.016 ms
heap_gc_collect                    1 call     619.050 ms
gc_collect_total                   1 call     619.048 ms
gc_trace_objects                   1 call     592.056 ms
gc_compact_data                    1 call      11.937 ms
gc_sweep                           1 call       9.816 ms
```

The sweep now still walks the same 4,450,856 objects, but ownership
classification is no longer the bottleneck:

```text
gc_sweep_walked_objects       4450856
gc_sweep_dead_objects         4444918
gc_sweep_bump_owned             24338
gc_sweep_object_zone_owned    4420580
gc_sweep_freed_bytes        110323710
```

Current Cube3D bottleneck after this slice: the one GC is still visible, but
its time is now mostly object tracing/mark processing (`gc_trace_objects`),
while regular `property_set` self time is down to the low-second range in the
instrumented run. The prior `array_push_direct_expand` attribution was an
ownership-classification artifact, not array growth cost.

## Profile Snapshot

The following profile runs used `lambda-profile.exe` with `JS_EXEC_PROFILE=2`.
Instrumentation changes absolute runtime, so these numbers should be used as
hot-path attribution, not as benchmark timing claims.

### `jetstream/cube3d`

Top event self time:

```text
property_set       calls=403711    self_ms=63737.027
call_function      calls=105067    self_ms=2821.322
property_access    calls=2019142   self_ms=79.205
property_get       calls=2062715   self_ms=44.816
array_get_int      calls=2072800   self_ms=41.321
```

Hot IC site examples:

```text
Q.NumPx@323:27        load probes=43232  miss_not_map=43232
Q.LinePixels@323:41   load probes=43200  miss_not_map=43200
Q.LastPx@65:15        load probes=14816  miss_not_map=14816
Q.LastPx@76:7         store probes=14816 miss_not_map=14816
Q.Line@238:7          store probes=1664  miss_not_map=1664
expr.V@254:9          store probes=14688 megamorphic=14684
```

Diagnosis: the hottest named properties are on arrays (`Q.LastPx`, `Q.NumPx`,
`Q.LinePixels`, `Q.Line`). Tune11's plain-map IC rejects these because the
receiver is `LMD_TYPE_ARRAY`, not `LMD_TYPE_MAP`.

### `awfy/deltablue`

Top event self time:

```text
call_function      calls=1182583   self_ms=2264.164
property_get       calls=3302643   self_ms=1526.046
property_set       calls=251198    self_ms=274.821
property_access    calls=874755    self_ms=34.647
get_slot_i         calls=752426    self_ms=13.455
```

Store IC is effective:

```text
store_ic_probe      363564
store_ic_hit_mono   346862
store_ic_hit_poly   6
store_ic_miss       1713
store_ic_megamorphic 14994
```

Hot examples:

```text
expr.value@901:7    store probes=204000 hit=203999
expr.value@935:7    store probes=46260  hit=46259
i.mark@699:30       store probes=14120  hit=14119
```

Remaining miss examples:

```text
s.arithmeticValue@635:35 load probes=24120 miss_not_plain=24120
this.output@873:12       load probes=6940  megamorphic=6921
this.strength@669:5      store probes=6120 megamorphic=6017
```

Diagnosis: ES class shaping and store IC are already paying off. The remaining
work is mostly descriptor/non-plain read cases, polymorphic method/constraint
objects, and call dispatch.

### `jetstream/deltablue`

Top event self time:

```text
property_get       calls=11966502  self_ms=3871.747
call_function      calls=2266330   self_ms=2509.538
property_access    calls=5366124   self_ms=211.775
property_set       calls=442872    self_ms=86.928
```

Aggregate IC behavior:

```text
load_ic_probe       3553399
load_ic_hit_mono    640543
load_ic_hit_poly    79933
load_ic_miss        29324
load_ic_megamorphic 2803686

store_ic_probe      346772
store_ic_hit_mono   442
store_ic_hit_poly   1536
store_ic_miss       352
store_ic_megamorphic 344466
```

Hot examples:

```text
this.elms@70:10       load probes=360540 megamorphic=360532
this.direction@413:11 load probes=274340 megamorphic=274320
this.v2@413:50        load probes=263840 megamorphic=263820
expr.value@525:3      store probes=204000 megamorphic=203996
out.mark@187:3        store probes=10140  megamorphic=10136
```

Diagnosis: this benchmark uses prototype-style constructors and hand-written
inheritance:

```js
Object.defineProperty(Object.prototype, "inheritsFrom", ...)
Ctor.inheritsFrom(BaseCtor)
BaseCtor.call(this, ...)
this.field = ...
```

Tune11's ES class constructor pre-shaping does not cover this pattern, so
equivalent instances still reach hot sites with many distinct `TypeMap*`
identities.

## Candidate Phases

### P1: Array Named-Property IC

Target: `jetstream/cube3d`.

Problem:

Arrays in JavaScript can have ordinary named properties in addition to indexed
elements. LambdaJS stores those named properties through an array companion map
(`Array::extra`). Current callsite IC only handles `LMD_TYPE_MAP` receivers, so
array named properties are rejected before slot lookup.

Proposal:

Add a fixed-name fast path for array named properties:

```text
array receiver
  -> key is fixed non-index property name
  -> array has companion map
  -> companion map is descriptor-free plain map
  -> companion shape matches cached shape
  -> read/write cached slot
```

This can be implemented either as:

1. an extension of `js_property_access_named_ic()` and
   `js_property_set_named_ic()` with `LMD_TYPE_ARRAY` handling; or
2. separate helpers such as `js_array_named_property_access_ic()` and
   `js_array_named_property_set_ic()`.

The first option keeps MIR lowering simpler. The second option keeps array
semantics isolated. The first implementation can start inside the existing IC
helper as long as the array branch remains small and well-profiled.

Cache entry shape:

```c
typedef struct JsNamedICEntry {
    uint8_t receiver_kind;      // map or array-companion
    void* shape;               // TypeMap* of map or array companion map
    void* entry;               // ShapeEntry*
    int64_t byte_offset;
} JsNamedICEntry;
```

For array receivers, the cache shape is the companion map's `TypeMap*`, not the
array storage shape.

Install rules:

- only non-index string names;
- only if `arr->extra` exists after the normal semantic path;
- only descriptor-free own data slots;
- never bypass length semantics or numeric-index semantics;
- writes install only after the ordinary setter succeeds.

Miss/fallback:

- no companion map: fallback;
- numeric key: fallback;
- descriptor/accessor property: fallback;
- inherited property: fallback;
- array exotic length behavior: fallback.

Suggested flag:

```text
LAMBDA_JS_ARRAY_NAMED_IC=0
```

Acceptance:

- `jetstream/cube3d` should show lower `miss_not_map` for `Q.LastPx`,
  `Q.NumPx`, `Q.LinePixels`, and `Q.Line`.
- `make test262-baseline` must remain 0-regression.

### P2: Prototype-Style Constructor Shape Sharing

Target: `jetstream/deltablue`.

Problem:

Tune11 P4/P5 handles ES class constructor pre-shaping and explicit derived
class composition. JetStream DeltaBlue uses older constructor functions and
prototype inheritance. The runtime sees many same-layout objects as different
shape identities, causing load/store IC sites to become megamorphic.

Proposal:

Extend constructor field collection and pre-shaped allocation to plain function
constructors:

```js
function Foo(a, b) {
  this.a = a;
  this.b = b;
}
```

and inherited constructor-call patterns:

```js
function Bar(x, y, z) {
  Foo.call(this, x, y);
  this.z = z;
}
Bar.inheritsFrom(Foo);
```

Initial scope:

- collect unconditional `this.name = ...` assignments in function constructor
  bodies;
- identify direct `Super.call(this, ...)` constructor chaining;
- compose base-first field metadata where the super constructor target is
  statically known;
- allocate shared final shapes at `new Foo(...)` and `new Bar(...)` sites;
- leave all dynamic, aliased, conditional, or unknown patterns on the current
  path.

Implementation hooks:

- reuse the constructor field metadata machinery already used by class
  pre-shaping;
- add function-constructor entries next to class entries, or a shared
  constructor-shape metadata structure;
- teach call/new lowering that a plain function can have constructor shape
  metadata;
- keep detach guards from Tune11 when a shared constructor shape is mutated
  incompatibly.

Pattern detection should be conservative. Examples that should not be
pre-shaped initially:

```js
this[x] = v;
if (cond) this.a = v;
other.call(this);
factory().call(this);
return explicitObject;
```

Suggested flag:

```text
LAMBDA_JS_FUNC_CTOR_SHAPE=0
```

Acceptance:

- `jetstream/deltablue` should show lower `load_ic_megamorphic` and
  `store_ic_megamorphic` at:
  - `this.elms@70:10`
  - `this.direction@413:11`
  - `this.v2@413:50`
  - `expr.value@525:3`
  - `out.mark@187:3`
- `awfy/deltablue` should not regress.
- `make test262-baseline` must remain 0-regression.

### P3: Name Identity in Shape Entries and IC Metadata

Target: support P1/P2/P4 and future transition caches.

Problem:

Current fixed-name ICs mostly use stable name pointers and lengths, with string
comparison fallback. This is acceptable for hit paths, but name identity would
make shape lookup, transition lookup, and debug validation cleaner.

Proposal:

Assign stable compact identity to namepool names:

```text
"b" -> name_id
```

Then store the identity in:

- `ShapeEntry`;
- `JsLoadIC` / `JsStoreIC`;
- named-property profile labels where useful;
- future shape-transition keys.

The target cache key becomes:

```text
TypeMap* + name_id -> ShapeEntry* / byte_offset
```

This phase should not try to replace all string comparisons at once. A safe
first step:

- add `name_id` where namepool strings are created;
- populate `ShapeEntry::name_id`;
- compare `name_id` when both sides have one;
- keep pointer/length/string comparison as fallback;
- add debug assertions that name id and string identity agree.

Runtime paths covered by the name-id follow-up:

- TypeMap hash lookup exposes a by-id variant so IC install/build paths and
  future descriptor ICs do not recompute the name hash after the callsite
  already computed or cached it.
- Fixed-name IC hit checks test pointer/length identity before computing
  `name_id`, because compiled property sites usually reuse the same name
  pointer after the first install.
- Shape-transition lookup stores the transition key's `name_id` and uses it as
  an early reject before byte comparison; this helps repeated growth from
  shared constructor shapes and transition shapes.
- Shape-entry clones and pre-shaped constructor entries copy or eagerly compute
  `name_id`, so cloned/shared shapes do not pay lazy initialization in later IC
  installs.

Expected impact:

This is probably a modest standalone win. Its real value is reducing miss and
install overhead for P1/P2/P4 and enabling efficient transition lookup.

### P4: Descriptor Own-Data Read IC

Target: `awfy/deltablue` and descriptor-heavy object code.

Problem:

Tune11 intentionally keeps the fast path `MAP_KIND_PLAIN` only. Profiles show
some hot reads miss because the receiver is not plain, for example:

```text
s.arithmeticValue@635:35 load probes=24120 miss_not_plain=24120
```

Proposal:

Add a second-tier read IC install case for descriptor maps when the ordinary
lookup proves the property is:

- own;
- data, not accessor;
- stable slot-backed;
- not deleted;
- safe to read without prototype walk.

This should be separate from the current plain-map fast hit, so the common
plain case remains minimal.

Possible state split:

```text
entry.kind = PLAIN_DATA_SLOT
entry.kind = DESC_DATA_SLOT
```

The descriptor case can keep an extra validation guard if needed. It should
not cache accessor properties or inherited properties.

Suggested flag:

```text
LAMBDA_JS_DESC_READ_IC=0
```

Acceptance:

- reduce `miss_not_plain` for known own-data descriptor sites;
- no accessor/prototype regressions in js262;
- no measurable regression in plain-map IC hit speed.

### P5: Stable Callee Call IC

Target: DeltaBlue family after P1/P2/P4 reduce property noise.

Problem:

Both DeltaBlue profiles still spend significant time in call dispatch:

```text
awfy/deltablue       call_function self_ms=2264.164
jetstream/deltablue  call_function self_ms=2509.538
```

Proposal:

Add a small callsite cache for stable JS function callees:

```text
callee identity + expected arity/classification -> direct call setup
```

Candidate call forms:

- direct local function calls;
- method calls where property IC returns the same function identity;
- constructor calls with stable constructor identity.

This should come after P1/P2, because many callsites currently pay property
lookup cost before dispatch. Once receiver/callee lookup is stable, call IC
measurements will be cleaner.

Suggested flag:

```text
LAMBDA_JS_CALL_IC=0
```

Acceptance:

- reduce `call_function` self time on DeltaBlue and Richards-like tests;
- no change to `this`, `new.target`, derived constructor, spread, arguments,
  or bound-function semantics.

## Measurement Plan

Use release or release-profile builds only.

Correctness:

```bash
make release
make test262-baseline
git diff --check
```

Benchmark A/B:

```bash
python3 test/benchmark/run_benchmarks.py -e lambdajs,nodejs -s jetstream -b cube3d -n 3 --no-save
python3 test/benchmark/run_benchmarks.py -e lambdajs,nodejs -s jetstream -b deltablue -n 3 --no-save
python3 test/benchmark/run_benchmarks.py -e lambdajs,nodejs -s awfy -b deltablue -n 3 --no-save
```

Profile A/B:

```bash
JS_EXEC_PROFILE=2 JS_EXEC_PROFILE_OUT=temp/profile_cube3d.tsv \
  ./lambda-profile.exe js temp/profile_jetstream_cube3d.js --no-log

JS_EXEC_PROFILE=2 JS_EXEC_PROFILE_OUT=temp/profile_awfy_deltablue.tsv \
  ./lambda-profile.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log

JS_EXEC_PROFILE=2 JS_EXEC_PROFILE_OUT=temp/profile_jetstream_deltablue.tsv \
  ./lambda-profile.exe js temp/profile_jetstream_deltablue.js --no-log
```

When measuring a phase, also run with the phase flag disabled. For example:

```bash
LAMBDA_JS_ARRAY_NAMED_IC=0 ...
LAMBDA_JS_FUNC_CTOR_SHAPE=0 ...
```

## Expected Payoff

Expected largest gains:

1. P1 should help `jetstream/cube3d`, currently the worst ratio and dominated
   by named properties on arrays.
2. P2 should help `jetstream/deltablue`, whose ICs are mostly megamorphic under
   prototype-style constructor patterns.
3. P4/P5 should help DeltaBlue and Richards-style object/method workloads after
   shape identity is stable enough for callsites to learn.

P3 is mainly infrastructure. It should be accepted if it simplifies slot and
transition lookup without regressing js262 or benchmark timings.

## Risks

- Array named properties are easy to confuse with numeric-index and `length`
  semantics. P1 must only target fixed non-index names and must install after
  the ordinary semantic path proves the final storage.
- Prototype-style constructor inference can become unsound if it tries to cover
  dynamic JavaScript patterns too early. P2 should be conservative and leave
  unknown cases untouched.
- Descriptor ICs can accidentally bypass accessors or prototype effects. P4
  must cache only own data properties.
- Call ICs can break `this`, `new.target`, bound functions, spread calls, and
  derived constructors. P5 should start with a very narrow direct-call case.

## Open Questions

1. Should the array named-property IC reuse `JsLoadIC` / `JsStoreIC`, or should
   it introduce separate named-array IC structs to keep the plain-map hit path
   untouched?
2. Should function-constructor shape metadata share the class metadata structs,
   or should it be represented separately and merged only at allocation time?
3. Should `name_id` be stored directly in `Name`/`String`, or should there be a
   side table to avoid changing the broader string representation? Current P3
   answer: keep it on `ShapeEntry`, IC metadata, and transition metadata only.
   That captures the hot property paths without changing the broader string
   representation.
4. Should descriptor own-data IC be read-only first, or should a descriptor
   store IC be added in the same phase?
5. How narrow should the first call IC be: direct local calls only, or method
   calls with stable callee identity as well?

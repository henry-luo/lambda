# Transpile JS Tune11 Proposal: Plain-Map Property Lookup ICs

Date: 2026-06-24
Status: implemented first slice; js262 clean

Primary sources:

- Plain-map property read path: `lambda/js/js_runtime.cpp`
- Ordinary property kernels: `lambda/js/js_props.cpp`, `lambda/js/js_props.h`
- Shape and TypeMap storage: `lambda/lambda-data.hpp`
- MIR member lowering: `lambda/js/js_mir_expression_lowering.cpp`
- Existing shaped-slot helpers: `js_get_shaped_slot`, `js_shape_slot_guard`
- Benchmark source: `test/benchmark/Overall_Result7.md`, `test/benchmark/benchmark_results_v3.json`

## Goal

Reduce the cost of compiled named property reads such as `a.b` on ordinary
plain maps without changing JavaScript property semantics.

This pass has three implementation candidates:

0. Split ordinary maps into descriptor-free `MAP_KIND_PLAIN` and
   descriptor-bearing `MAP_KIND_DESC`.
1. Remove the duplicate shape lookup in `js_own_shape_slot_status()`.
2. Add a callsite inline cache for compiled non-computed member reads, with both
   monomorphic and small polymorphic states.

The candidates must be measured and accepted separately where possible. P0 is
the invariant that lets P2 have a cheap hit path. P1 is a narrow runtime
cleanup. P2 is a larger compiler/runtime fast path and must keep a complete
fallback to the current `js_property_access()` behavior.

## Implementation Update: 2026-06-24

This first slice has been implemented:

- added `MAP_KIND_DESC` and kept `MAP_KIND_PLAIN` for descriptor-free ordinary
  shape-backed maps;
- added ordinary-shape checks that include both `MAP_KIND_PLAIN` and
  `MAP_KIND_DESC`;
- promoted ordinary maps to `MAP_KIND_DESC` before descriptor flag mutation
  paths that were audited in this pass;
- removed the duplicate lookup in `js_own_shape_slot_status()` by reading the
  already-found `ShapeEntry` slot directly when data/offset validation passes;
- added helper-based callsite load IC support for compiled fixed-name member
  reads, with monomorphic, polymorphic, and megamorphic states;
- kept IC fast hits plain-map-only and reduced the fast-hit guard to receiver
  map, `MAP_KIND_PLAIN`, data pointer, shape pointer, and byte-offset bounds;
- moved descriptor/accessor/deleted validation to IC install time;
- added the runtime flag `LAMBDA_JS_LOAD_IC=0` to disable the new load IC path
  for A/B measurement and emergency gating.

The helper keeps the old semantic path as the miss/fallback path, so inherited
properties, accessors, deleted slots, builtin fallback, primitives, and exotic
objects continue through `js_property_access()`.

### Verification

Release build and correctness checks:

```bash
make build-test
./lambda.exe js temp/tune11_ic_smoke.js --no-log
LAMBDA_JS_LOAD_IC=0 ./lambda.exe js temp/tune11_ic_smoke.js --no-log
./lambda.exe js -e 'let o={a:1,b:2}; console.log(o.a,o["b"]); let p={x:1}; Object.defineProperty(p,"x",{get:function(){return 9;}, configurable:true}); console.log(p.x);' --no-log
make test262-baseline
env LAMBDA_JS_LOAD_IC=0 make test262-baseline
git diff --check
```

Observed smoke output for the inline `defineProperty` probe:

```text
1 2
9
```

Quiet-machine rerun after background tasks were stopped, using the release
executable:

```bash
env LAMBDA_JS_LOAD_IC=0 make test262-baseline
make test262-baseline
```

Back-to-back js262 results:

| Mode | Fully passed | Failed | Regressions | Total wall | Batch execute |
|------|-------------:|-------:|------------:|-----------:|--------------:|
| `LAMBDA_JS_LOAD_IC=0` | 40261 / 40261 | 0 | 0 | 92.7s | 92.3s |
| Load IC enabled | 40261 / 40261 | 0 | 0 | 94.5s | 94.1s |

The js262 correctness gate is clean. The back-to-back timing delta is about
+1.8s / +1.9% wall time for the helper-based IC version. This does not show a
suite-level win, and suggests the helper-call overhead can outweigh the current
fast path on js262-shaped workloads.

### Benchmark Results

Full LambdaJS benchmark sweep, one run per benchmark, no JSON write:

```bash
env LAMBDA_JS_LOAD_IC=0 python3 test/benchmark/run_benchmarks.py -e lambdajs -n 1 --no-save > temp/tune11_bench_full_noic.txt 2>&1
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 1 --no-save > temp/tune11_bench_full_ic.txt 2>&1
```

Result: 62 benchmarks were attempted in each mode; 58 reported timings in each
mode. Existing heavy LambdaJS timeout/failure cases in this run were:

- `awfy/havlak`;
- `awfy/cd`;
- `jetstream/navier_stokes`;
- `jetstream/hashmap`.

Across the 58 common timed results, `geomean(no-IC time / IC time) = 1.0036`,
or a +0.36% one-run speedup for IC enabled. That is effectively neutral at this
sample size.

Notable full-sweep rows:

| Benchmark | Load IC | No load IC | Delta |
|-----------|--------:|-----------:|------:|
| `awfy/richards` | 6.71s | 6.64s | -1.0% |
| `awfy/deltablue` | 3.71s | 3.67s | -1.1% |
| `jetstream/cube3d` | 65.70s | 67.53s | +2.8% |
| `jetstream/richards` | 835ms | 806ms | -3.5% |
| `jetstream/splay` | 1.58s | 1.57s | -0.6% |
| `jetstream/deltablue` | 2.28s | 2.27s | -0.4% |
| `jetstream/crypto_sha1` | 243ms | 244ms | +0.4% |
| `jetstream/raytrace3d` | 2.42s | 2.47s | +2.1% |

Focused property-heavy A/B, three-run medians, no JSON write:

```bash
env LAMBDA_JS_LOAD_IC=0 python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3 --no-save -s awfy,jetstream -b richards,deltablue,splay,cube3d,crypto_sha1,raytrace3d > temp/tune11_bench_focus_noic.txt 2>&1
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3 --no-save -s awfy,jetstream -b richards,deltablue,splay,cube3d,crypto_sha1,raytrace3d > temp/tune11_bench_focus_ic.txt 2>&1
```

| Benchmark | Load IC | No load IC | Delta |
|-----------|--------:|-----------:|------:|
| `awfy/richards` | 6.72s | 6.61s | -1.6% |
| `awfy/deltablue` | 3.71s | 3.67s | -1.1% |
| `jetstream/cube3d` | 66.14s | 65.06s | -1.6% |
| `jetstream/richards` | 815ms | 806ms | -1.1% |
| `jetstream/splay` | 1.58s | 1.58s | 0.0% |
| `jetstream/deltablue` | 2.27s | 2.26s | -0.4% |
| `jetstream/crypto_sha1` | 240ms | 241ms | +0.4% |
| `jetstream/raytrace3d` | 2.40s | 2.38s | -0.8% |

Focused geomean across the eight common results was -0.79% for IC enabled.

Interpretation:

- correctness is the decisive result for this slice: js262 has zero
  regressions with the IC enabled;
- full-suite benchmark impact is effectively neutral in a one-run sweep;
- focused property-heavy medians are slightly negative for the current
  helper-call IC;
- the current helper-call IC does not yet produce a defensible broad benchmark
  win;
- a follow-up should inline at least the monomorphic fast guard in MIR before
  expecting larger speedups.

## Current Shape

Today, for ordinary JS objects, `MAP_KIND_PLAIN` skips exotic dispatch but still
uses the normal `[[Get]]` pipeline:

1. `js_property_access(object, key)`
2. `js_property_get(object, key)`
3. `js_ordinary_get_own(object, key, Receiver, &value)`
4. `js_prototype_lookup_ex(object, key, &found)`
5. built-in fallback or `undefined`

The TypeMap already has a per-shape name hash:

- `TypeMap::field_index`
- optional `TypeMap::field_index_dynamic`
- `typemap_hash_lookup(TypeMap*, const char*, int)`

This means a global `(Shape*, Name*) -> ShapeEntry*` cache would overlap with
the existing per-shape hash. The remaining hot cost is that many compiled
`a.b` reads still pay runtime call overhead, key plumbing, own-property status
checks, prototype fallback machinery on misses, and sometimes duplicate shape
lookups.

## P0: Split Plain Maps from Descriptor Maps

### Current issue

`MAP_KIND_PLAIN` currently means "ordinary, non-exotic shape-backed map." It
does not mean descriptor-free. A plain map can still carry descriptor state in
`ShapeEntry::flags`, including:

- `JSPD_IS_ACCESSOR`;
- `JSPD_DELETED`;
- `JSPD_NON_WRITABLE`;
- `JSPD_NON_ENUMERABLE`;
- `JSPD_NON_CONFIGURABLE`.

That forces a callsite IC hit to re-check accessor/deleted state if it only
guards on `MAP_KIND_PLAIN` and shape pointer.

### Proposed implementation

Introduce a second ordinary map kind:

```cpp
MAP_KIND_PLAIN = 0, // ordinary shape-backed map with default data descriptors
MAP_KIND_DESC  = 13 // ordinary shape-backed map with descriptor metadata
```

New invariant:

- `MAP_KIND_PLAIN` means every own shape entry is a default data property:
  `ShapeEntry::flags == 0`.
- `MAP_KIND_DESC` means the map is still ordinary and shape-backed, but it has
  or has had non-default descriptor state.
- Promotion is monotonic. Once a map becomes `MAP_KIND_DESC`, do not downgrade
  it in this tuning pass.

Descriptor promotion triggers:

- accessor install or accessor clearing;
- `Object.defineProperty` / `Reflect.defineProperty` descriptor updates;
- delete tombstones;
- freeze/seal/prevent-extension descriptor writes;
- direct helpers that make properties non-writable, non-enumerable, or
  non-configurable;
- any direct `ShapeEntry::flags` mutation that sets a non-zero flag.

Because `MAP_KIND_DESC` is still an ordinary object, every existing site that
means "ordinary shape-backed object" must use a helper such as:

```cpp
static inline bool js_map_kind_is_ordinary_shape(uint8_t map_kind) {
    return map_kind == MAP_KIND_PLAIN || map_kind == MAP_KIND_DESC;
}
```

Only true descriptor-free fast paths should require
`map_kind == MAP_KIND_PLAIN`.

### Correctness constraints

- Do not route `MAP_KIND_DESC` to exotic map dispatch.
- Do not change object/prototype/builtin fallback semantics.
- Keep descriptor state per-map: promotion must compose with the existing
  TypeMap clone-on-mutation path.
- Audit direct `jspd_set_*` uses. Either replace them with descriptor helpers or
  promote the owning map before the flag is changed.
- Arrays' companion maps and function `properties_map` may use descriptor
  state; do not accidentally classify them as fast plain receivers for P2.

### Expected benefit

P0 gives P2 a cheap, meaningful invariant. For `MAP_KIND_PLAIN`, a shape guard
can prove that the cached property is an own data property with default
descriptor semantics. Accessor/deleted checks move to the install/miss path.
Descriptor-bearing ordinary maps still work through the existing slow path.

## P1: Remove Duplicate Lookup in `js_own_shape_slot_status()`

### Current issue

`js_own_shape_slot_status()` currently performs:

```cpp
ShapeEntry* se = js_find_shape_entry(object, name, name_len);
Item slot = js_map_get_fast_ext(m, name, name_len, &found);
```

For the common plain-map own data/accessor case, both calls resolve the same
name through the same `TypeMap` hash. `js_find_shape_entry()` finds the
`ShapeEntry`; `js_map_get_fast_ext()` then calls the map lookup path again to
read the value.

### Proposed implementation

Change `js_own_shape_slot_status()` so the common path reads directly from the
already-found `ShapeEntry`:

```cpp
ShapeEntry* se = js_find_shape_entry(object, name, name_len);
if (se) {
    Item slot = _map_read_field(se, m->data);
    if (out_slot) *out_slot = slot;
    if (out_se) *out_se = se;
    if (jspd_is_deleted(se)) return JS_SHAPE_SLOT_DELETED;
    if (js_props_is_deleted_sentinel(slot)) return JS_SHAPE_SLOT_DELETED;
    if (jspd_is_accessor(se)) return JS_SHAPE_SLOT_ACCESSOR;
    return JS_SHAPE_SLOT_DATA;
}
```

Only when no `ShapeEntry` is found should the function call
`js_map_get_fast_ext()`. That preserves the existing fallback for spread/nested
maps and any non-standard map path that can return a value even when the direct
shape entry is absent.

### Correctness constraints

- Do not change `JS_OWN_NOT_FOUND`, `JS_OWN_DELETED`, and `JS_OWN_READY`
  behavior.
- Preserve accessor dispatch in `js_ordinary_get_own()`.
- Preserve setter-only public accessors returning `undefined`.
- Preserve private setter-only TypeError behavior.
- Preserve array companion-map descriptors and function `properties_map`.
- Preserve spread/nested map fallback when `se == NULL`.
- Keep deleted slots authoritative via `JSPD_DELETED` and the retained raw
  deleted sentinel check.

### Expected benefit

This should reduce self time in `property_get` / `property_access` without
changing generated MIR. It should help all ordinary own-property reads,
including runtime reads that do not come from compiled `a.b` sites.

The expected improvement is modest but low risk: one duplicate hash/probe and
one duplicate name comparison disappear for common own properties.

## P2: Callsite Inline Cache for Compiled `a.b`

### Definition

A callsite inline cache stores lookup results at a specific compiled property
read site.

For:

```js
sum += point.x;
```

the `point.x` site can remember that a particular `TypeMap*` has own data
property `"x"` at a specific `ShapeEntry` / byte offset. On later executions,
the site checks the receiver shape pointer and reads the field directly.

This is different from a global `(Shape*, Name*)` hash:

- a global cache still performs a table lookup on every access;
- a callsite IC hit performs a shape-pointer comparison and a direct slot read;
- a polymorphic callsite IC performs a small fixed linear scan over a few
  observed shapes, then direct slot read on hit.

### Scope

Apply the IC only to compiled member reads where the property name is fixed:

- include: `a.b`
- include after existing special cases: `expr.b`
- exclude initially: `a[b]`, `a?.b`, `super.b`, private fields, and computed
  property names
- preserve existing earlier special paths for `document.x`, `Math.x`,
  typed-array `.length`, DOM style/dataset/classList reads, class static
  getters, and existing shaped-slot class-field reads

The IC should replace only the final general fallback in `jm_transpile_member()`
that currently emits `js_property_access(obj, "b")`.

### Runtime structure

Use fixed-size C-compatible structures. Do not use STL containers.

```cpp
#define JS_LOAD_IC_POLY_MAX 4

typedef struct JsLoadICEntry {
    TypeMap* shape;
    ShapeEntry* entry;
    int64_t byte_offset;
} JsLoadICEntry;

typedef struct JsLoadIC {
    uint8_t state;      // empty, mono, poly, megamorphic
    uint8_t count;      // number of populated entries
    uint16_t miss_count;
    const char* name;   // interned/namepool property chars
    int name_len;
    Item key_item;      // optional cached string Item if lifetime/rooting is safe
    JsLoadICEntry entries[JS_LOAD_IC_POLY_MAX];
} JsLoadIC;
```

`key_item` is optional in the first slice. If it is used, confirm the string
lifetime and GC/rooting story. Otherwise the helper can receive the existing
boxed string key from MIR on the miss path.

### Runtime helper

Add a helper shaped like:

```cpp
extern "C" Item js_property_access_named_ic(
    Item object, const char* name, int name_len, JsLoadIC* ic);
```

Fast hit checks after P0:

1. receiver is `LMD_TYPE_MAP`;
2. `map_kind == MAP_KIND_PLAIN`;
3. `m->data != NULL`;
4. `m->type == entry.shape`;
5. cached `ShapeEntry*` is non-null;
6. cached byte offset is in `m->data_cap`.

On hit, return `_map_read_field(entry, m->data)`.

The install/miss path performs the heavier validation:

- the shape entry exists and matches the requested name;
- `ShapeEntry::flags == 0`;
- byte offset is within `m->data_cap`;
- the slot value is not the deleted sentinel.

On miss, use the current ordinary path. For cache population, install an entry
only when the receiver is `MAP_KIND_PLAIN` and has an own data property. Do not
install for:

- absent properties;
- inherited properties;
- built-in fallback properties;
- accessors;
- deleted slots;
- Proxy, DOM, CSSOM, TypedArray, ArrayBuffer, DataView, process.env, document
  proxy, iterator, array companion maps, or any other non-plain map kind;
- primitive wrappers and strings;
- exceptions.

### IC state machine

Initial state:

- no cached shape;
- miss calls the current lookup path;
- if the receiver has an own plain-map data property, install the first entry
  and become monomorphic.

Monomorphic state:

- one `(shape, entry, byte_offset)` record;
- shape hit returns direct field value;
- shape miss attempts to install a second own-data entry and becomes
  polymorphic;
- semantic miss falls back without caching.

Polymorphic state:

- fixed linear scan of up to `JS_LOAD_IC_POLY_MAX` entries;
- shape hit returns direct field value;
- own-data miss with a new shape appends while capacity remains;
- after capacity is exceeded or miss count crosses a threshold, become
  megamorphic.

Megamorphic state:

- skip IC probing and call the existing general path;
- this avoids wasting time on sites with many unrelated receiver shapes.

### Invalidations and safety

The IC should be guard-based rather than mutation-invalidation based.

Safe cases:

- ordinary value writes keep the same shape and slot, so direct reads see the
  new value;
- adding a new property usually changes the receiver `TypeMap*`, so the shape
  guard fails;
- `defineProperty`, accessor install, attribute mutation, and `delete` promote
  the receiver from `MAP_KIND_PLAIN` to `MAP_KIND_DESC`, so the fast hit guard
  fails before reading;
- if a path cannot guarantee promotion before descriptor mutation, P2 must not
  rely on that path for `MAP_KIND_PLAIN` until it is repaired.

Required reset:

- Any IC that stores raw `TypeMap*` or `ShapeEntry*` must be cleared on runtime
  batch/heap reset or scoped to the compiled function lifetime that owns those
  objects.
- The implementation must audit existing `shape_cache_ptr` lifetime handling
  before copying that pattern, because the IC pointer must outlive the compiled
  MIR code that uses it.

### MIR integration

In `jm_transpile_member()`:

1. keep existing special cases first;
2. at the final general non-computed named-property fallback, allocate or
   reference a per-site `JsLoadIC`;
3. emit `js_property_access_named_ic(obj, name_chars, name_len, ic_ptr)`;
4. keep the old `js_property_access(obj, key)` path as the helper's slow path.

The first implementation can keep the guard inside the helper call. If that
shows a measurable win and high hit rate, a later pass can inline the mono/PIC
guard in MIR and call the helper only on misses.

### Profiling counters

Add profiler-only counters before broad enablement:

- `load_ic_probe`
- `load_ic_hit_mono`
- `load_ic_hit_poly`
- `load_ic_miss`
- `load_ic_install_mono`
- `load_ic_install_poly`
- `load_ic_megamorphic`

Add a small `# Load IC sites` profiler section with:

- site label, such as `obj.prop@line:column`;
- hit counts;
- miss counts;
- state;
- sampled shape pointers.

This mirrors the useful Tune10 shape-guard evidence and prevents accepting a
patch that only looks good in aggregate call counts.

## Acceptance Plan

Each candidate must be accepted independently.

### P0 acceptance

1. Add `MAP_KIND_DESC` and ordinary-shape helper predicates.
2. Promote maps before descriptor flag mutation.
3. Replace ordinary-object checks that should include descriptor maps.
4. Build tests.
5. Run JS gtests.
6. Run the full js262 baseline gate.
7. Keep only if descriptor/accessor/delete/prototype behavior is unchanged.

### P1 acceptance

1. Implement only the `js_own_shape_slot_status()` duplicate-lookup removal.
2. Build tests.
3. Run JS gtests.
4. Run the full js262 baseline gate.
5. Run a focused release benchmark/profile pass for property-heavy cases.
6. Keep only if correctness is unchanged and property-get/profile numbers do
   not regress.

### P2 acceptance

1. Implement helper-only callsite IC with profiler counters.
2. Keep the old slow path intact.
3. Run focused smoke tests that cover:
   - own data property;
   - accessor getter;
   - setter-only accessor;
   - delete then prototype fallback;
   - inherited data property;
   - built-in fallback;
   - `Object.defineProperty` data-to-accessor transition and
     `MAP_KIND_PLAIN` to `MAP_KIND_DESC` promotion;
   - object shape change after cache population;
   - Proxy and DOM/typed-array non-plain objects falling back.
4. Run JS gtests and js262.
5. Run focused property-heavy benchmarks with profiler counters.
6. Run the benchmark suite before accepting any broad claim.

## Performance Impact Checks

Performance checks must use a release build. Do not use debug builds for
performance numbers.

### Correctness gates

```bash
make build-test
./test/test_js_gtest.exe --gtest_brief=1
make release
make test262-baseline
```

If the test262 runner binary is missing after rebuild churn, restore it first:

```bash
make ensure-test262-gtest
```

### JS262 performance check

Use the full baseline gate as the correctness source of truth and compare its
reported wall-time / batch-time summary before and after each candidate:

```bash
make release
make test262-baseline
```

For a lower-noise direct runner comparison, use the current direct batch path:

```bash
./test/test_js_test262_gtest.exe --baseline-only --batch-only --run-async --async-list=test/js262/test262_baseline.txt --jobs=12 --write-failures=temp/tune11_js262_failures.tsv
```

Record:

- fully passing count;
- failed count;
- regressions;
- total wall time;
- sync and async batch time if printed;
- any changed slow clusters if a release-run artifact is captured.

### Benchmark check

Start with focused property-heavy benchmarks:

```bash
make release
python3 test/benchmark/run_benchmarks.py -e lambdajs -s awfy,jetstream -b richards,deltablue,cube3d,splay -n 3 --no-save
```

Then run the full LambdaJS benchmark pass:

```bash
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3 --no-save
```

If the result is strong and stable, rerun without `--no-save` only after
confirming the report-update plan:

```bash
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3
```

Record:

- geometric mean across timed LambdaJS benchmarks;
- changed missing/timeout cases;
- focused medians for `awfy/richards`, `awfy/deltablue`,
  `jetstream/richards`, `jetstream/deltablue`, `jetstream/cube3d`, and
  `jetstream/splay`;
- profiler hit/miss rates for the same focused cases.

### Profiler check

Use profiler runs to prove the mechanism, not as final wall-time evidence:

```bash
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/tune11_richards_ic.tsv ./lambda.exe js test/benchmark/awfy/richards2_bundle.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/tune11_deltablue_ic.tsv ./lambda.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/tune11_cube3d_ic.tsv ./lambda.exe js test/benchmark/jetstream/3d-cube.js --no-log
```

Expected profile signal for P2:

- `property_access` / `property_get` call counts decrease at hot member sites;
- IC hit counts dominate misses on stable sites;
- megamorphic sites are visible and do not keep probing forever;
- wall time improves in non-profile release runs, not only in profiler output.

## Keep / Revert Criteria

Keep P1 if:

- js262 has zero regressions;
- JS gtests pass;
- no accessor/delete/prototype behavior changes;
- property-heavy benchmark/profiler evidence is neutral or positive.

Keep P2 if:

- js262 has zero regressions;
- JS gtests pass;
- descriptor promotion is in place before IC enablement;
- focused semantic tests cover accessors, deletes, inherited properties,
  builtins, shape changes, and exotics;
- IC hit rates are high on stable property-heavy sites;
- focused benchmark medians improve;
- full `./test/benchmark` LambdaJS pass is neutral or positive overall.

Revert or gate P2 if:

- it creates any js262 regression;
- it changes getter/receiver semantics;
- it bypasses prototype or builtin fallback;
- any descriptor-bearing ordinary map remains incorrectly tagged
  `MAP_KIND_PLAIN`;
- it increases benchmark wall time despite reducing call counts;
- most hot sites become megamorphic or miss-heavy.

## Open Questions

1. Should the first P2 implementation use only a runtime helper, or should it
   inline the monomorphic guard in MIR immediately?
2. Where should per-site IC storage live so raw `TypeMap*` / `ShapeEntry*`
   pointers are cleared on heap reset and remain valid for compiled-code
   lifetime?
3. Should P2 share IC structs with existing class `shape_cache_ptr`, or keep a
   separate `JsLoadIC` registry to avoid coupling class-shape caches with
   general property-read caches?
4. Should the IC store `Item key_item`, or should it always reconstruct/pass the
   key on slow path until lifetime/rooting is proven?
5. Should future work ever downgrade a descriptor map back to `MAP_KIND_PLAIN`
   after all flags are cleared, or is monotonic promotion preferable forever?

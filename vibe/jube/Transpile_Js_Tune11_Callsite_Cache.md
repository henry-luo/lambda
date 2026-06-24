# Transpile JS Tune11 Proposal: Plain-Map Property Access ICs

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

Reduce the cost of compiled named property reads and writes such as `a.b` and
`a.b = v` on ordinary plain maps without changing JavaScript property
semantics.

This pass has four implementation candidates:

0. Split ordinary maps into descriptor-free `MAP_KIND_PLAIN` and
   descriptor-bearing `MAP_KIND_DESC`.
1. Remove the duplicate shape lookup in `js_own_shape_slot_status()`.
2. Add a callsite inline cache for compiled non-computed member reads, with both
   monomorphic and small polymorphic states.
3. Add a callsite inline cache for compiled non-computed member writes to
   existing own data properties, with both monomorphic and small polymorphic
   states.

The candidates must be measured and accepted separately where possible. P0 is
the invariant that lets P2 and P3 have cheap hit paths. P1 is a narrow runtime
cleanup. P2 is a larger compiler/runtime fast path and must keep a complete
fallback to the current `js_property_access()` behavior. P3 builds on the same
plain-map invariant for writes, but must keep the full `[[Set]]` path for every
case that is not a proven own data-property update.

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
  map, `MAP_KIND_PLAIN`, data pointer, and shape pointer in release builds;
- moved descriptor/accessor/deleted validation to IC install time;
- removed the bad pointer-sized alignment and pointer-sized bounds checks from
  `js_load_ic_offset_ok()`; debug builds still check `byte_offset < data_cap`;
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
| `LAMBDA_JS_LOAD_IC=0` | 40261 / 40261 | 0 | 0 | 109.4s | 109.0s |
| Load IC enabled | 40261 / 40261 | 0 | 0 | 110.5s | 110.1s |

The js262 correctness gate is clean. The back-to-back timing delta is about
+1.1s / +1.0% wall time for the helper-based IC version. This does not show a
suite-level win, and suggests the helper-call overhead can outweigh the current
fast path on js262-shaped workloads.

### Benchmark Results

Full LambdaJS benchmark sweep, one run per benchmark, no JSON write:

```bash
env LAMBDA_JS_LOAD_IC=0 python3 test/benchmark/run_benchmarks.py -e lambdajs -n 1 --no-save > temp/tune11_offsetfix_bench_full_noic.txt 2>&1
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 1 --no-save > temp/tune11_offsetfix_bench_full_ic.txt 2>&1
```

Result: 62 benchmarks were attempted in each mode; 58 reported timings in each
mode. Existing heavy LambdaJS timeout/failure cases in this run were:

- `awfy/havlak`;
- `awfy/cd`;
- `jetstream/navier_stokes`;
- `jetstream/hashmap`.

Across the 58 common timed results, `geomean(no-IC time / IC time) = 0.9988`,
or a -0.12% one-run delta for IC enabled. That is effectively neutral at this
sample size.

Notable full-sweep rows:

| Benchmark | Load IC | No load IC | Delta |
|-----------|--------:|-----------:|------:|
| `awfy/richards` | 6.90s | 6.89s | -0.1% |
| `awfy/deltablue` | 3.81s | 3.81s | 0.0% |
| `jetstream/cube3d` | 69.93s | 73.56s | +5.2% |
| `jetstream/richards` | 832ms | 834ms | +0.2% |
| `jetstream/splay` | 1.65s | 1.65s | 0.0% |
| `jetstream/deltablue` | 2.36s | 2.42s | +2.5% |
| `jetstream/crypto_sha1` | 250ms | 251ms | +0.4% |
| `jetstream/raytrace3d` | 2.50s | 2.53s | +1.2% |

Focused property-heavy A/B, three-run medians, no JSON write:

```bash
env LAMBDA_JS_LOAD_IC=0 python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3 --no-save -s awfy,jetstream -b richards,deltablue,splay,cube3d,crypto_sha1,raytrace3d > temp/tune11_offsetfix_bench_focus_noic.txt 2>&1
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3 --no-save -s awfy,jetstream -b richards,deltablue,splay,cube3d,crypto_sha1,raytrace3d > temp/tune11_offsetfix_bench_focus_ic.txt 2>&1
```

| Benchmark | Load IC | No load IC | Delta |
|-----------|--------:|-----------:|------:|
| `awfy/richards` | 6.90s | 6.90s | 0.0% |
| `awfy/deltablue` | 3.81s | 3.81s | 0.0% |
| `jetstream/cube3d` | 71.86s | 72.40s | +0.7% |
| `jetstream/richards` | 829ms | 833ms | +0.5% |
| `jetstream/splay` | 1.65s | 1.66s | +0.6% |
| `jetstream/deltablue` | 2.33s | 2.36s | +1.3% |
| `jetstream/crypto_sha1` | 248ms | 250ms | +0.8% |
| `jetstream/raytrace3d` | 2.49s | 2.54s | +2.0% |

Focused geomean across the eight common results was +0.74% for IC enabled.

### Load IC Hit-Rate Profile

Profile build command:

```bash
make build-release-profile
```

Focused profiler runs:

```bash
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/tune11_richards_ic_profile.tsv ./lambda-profile.exe js test/benchmark/awfy/richards2_bundle.js --no-log
LAMBDA_JS_LOAD_IC=0 JS_EXEC_PROFILE_OUT=temp/tune11_richards_noic_profile.tsv JS_EXEC_PROFILE=time ./lambda-profile.exe js test/benchmark/awfy/richards2_bundle.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/tune11_deltablue_ic_profile.tsv ./lambda-profile.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log
LAMBDA_JS_LOAD_IC=0 JS_EXEC_PROFILE_OUT=temp/tune11_deltablue_noic_profile.tsv JS_EXEC_PROFILE=time ./lambda-profile.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log
```

Observed load-IC aggregate counters:

| Benchmark | Probes | Mono hits | Poly hits | Misses | Megamorphic | Fast-hit rate |
|-----------|-------:|----------:|----------:|-------:|------------:|--------------:|
| `awfy/richards` | 4,094,400 | 4,844 | 34,642 | 2,958,295 | 1,096,638 | 0.96% |
| `awfy/deltablue` | 2,529,102 | 100 | 1,978 | 2,381,224 | 145,816 | 0.08% |

This proves the IC fast path is reachable, but the current insertion policy is
too broad. It sends many named member reads through `js_property_access_named_ic`
even when the receiver/property pair cannot install a plain-own-data entry.

Likely low-hit-rate causes:

- inherited properties and prototype methods: common JS patterns such as
  `obj.method` miss because the first implementation caches only own data
  properties;
- descriptor-bearing maps: function objects, class/prototype objects, array
  companion maps, and any object promoted to `MAP_KIND_DESC` are intentionally
  excluded;
- exotic and builtin receivers: typed arrays, DOM/CSSOM, process/env-like maps,
  strings/primitives, and builtin fallback cases still enter the helper if they
  reach the final named-member fallback;
- semantic misses do not currently disable a site: `miss_count` increments, but
  a site that repeatedly sees absent/inherited/descriptor/exotic cases can stay
  cache-empty and keep probing forever;
- megamorphic sites still pay the helper call before immediately falling back.

The most important next step is per-site load-IC profiling with fallback reason
counts. Aggregate counters show the problem, but they do not yet identify which
sites should be disabled, inlined, or given a different cache shape.

#### Richards Per-Site Breakdown

A profiling-only `# Load IC sites` section was added to the execution profiler
to split probes by site and miss reason.

Richards total by reason:

| Reason | Count | Share of probes |
|--------|------:|----------------:|
| `probe` | 4,094,400 | 100.0% |
| `miss_offset` | 2,958,200 | 72.3% |
| `megamorphic` | 1,096,638 | 26.8% |
| `hit_mono + hit_poly` | 39,486 | 1.0% |
| `install_mono + install_poly` | 76 | 0.0% |

Top `miss_offset` sites:

| Site | Probes | Meaning |
|------|-------:|---------|
| `this.taskHolding_@140:12` | 533,550 | `TaskState.isTaskHoldingOrWaiting()` boolean field |
| `this.packetPending_@140:35` | 505,750 | `TaskState.isTaskHoldingOrWaiting()` boolean field |
| `this.packetPending_@144:12` | 328,650 | `TaskState.isWaitingWithPacket()` boolean field |
| `this.handle@246:35` | 328,650 | `TaskControlBlock.runTask()` function/handle field |
| `this.taskWaiting_@140:58` | 300,300 | `TaskState.isTaskHoldingOrWaiting()` boolean field |

For these sites, the receiver is a plain map and the own `ShapeEntry` is found,
but `js_load_ic_offset_ok()` rejects the entry before install. The current IC
requires pointer-sized alignment even though the normal map reader,
`_map_read_field`, can read fields according to `ShapeEntry::type`. In Richards
this rejects many compact boolean/object fields that are otherwise hot own
properties.

Post check-fix rerun:

```bash
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/tune11_richards_offsetfix_ic_profile.tsv ./lambda-profile.exe js test/benchmark/awfy/richards2_bundle.js --no-log
```

Richards total by reason after removing pointer-sized alignment and
pointer-sized bounds checks from the release fast path:

| Reason | Count | Share of probes |
|--------|------:|----------------:|
| `probe` | 4,094,400 | 100.0% |
| `miss_offset` | 0 | 0.0% |
| `megamorphic` | 4,047,008 | 98.8% |
| `hit_mono + hit_poly` | 47,244 | 1.2% |
| `install_mono + install_poly` | 148 | 0.0% |

The offset check was therefore a real false-negative guard: the hot boolean
field sites are now installable. However, those same sites still go
megamorphic almost immediately because the IC currently keys on `TypeMap*` and
the benchmark creates many same-layout objects with distinct `TypeMap`
instances.

Selected post-fix hot sites:

| Site | Probes | Hits | Installs | Miss offset | Megamorphic |
|------|-------:|-----:|---------:|------------:|------------:|
| `this.taskHolding_@140:12` | 533,550 | 6 | 4 | 0 | 533,540 |
| `this.packetPending_@140:35` | 505,750 | 6 | 4 | 0 | 505,740 |
| `this.taskWaiting_@140:58` | 300,300 | 13 | 4 | 0 | 300,283 |

Top megamorphic sites:

| Site | Probes | Hits | Installs | Megamorphic |
|------|-------:|-----:|---------:|------------:|
| `message.link@237:20` | 116,300 | 0 | 4 | 116,296 |
| `packet.identity@430:29` | 116,100 | 0 | 4 | 116,096 |
| `dataRecord.workIn@309:38` | 116,400 | 4,652 | 4 | 111,744 |
| `workPacket_.datum@312:21` | 101,300 | 4,048 | 4 | 97,248 |
| `dataRecord.pending@282:41` | 92,500 | 3,696 | 4 | 88,800 |

These sites do install valid own data entries, but the IC keys only on
`TypeMap*`. Constructor-shaped objects currently allocate fresh `TypeMap`
instances, so same-layout objects can look like different shapes. After four
observed shapes, the site becomes megamorphic and falls back forever. The
existing class-shaped slot paths already work around this with structural shape
guards; the general callsite IC does not yet.

Richards-specific conclusions:

- The largest win is not widening the IC to prototypes. For this benchmark, the
  bulk of failed probes are hot own fields.
- `js_load_ic_offset_ok()` must not require pointer-sized alignment or
  pointer-sized bounds. The release fast path now trusts the installed
  `ShapeEntry` and shape guard; debug builds keep a narrow `byte_offset <
  data_cap` assertion-style check.
- Add a structural same-layout hit path for constructor-shaped objects:
  cached slot/name plus `slot_entries` or `js_shape_slot_guard`, then direct
  `_map_read_field`.
- Improve MIR class-field lowering so boolean/object `this.field` reads such as
  `TaskState` state flags use `js_get_shaped_slot` instead of reaching the
  generic named-load IC.
- Do not simply raise `JS_LOAD_IC_POLY_MAX`; per-object `TypeMap*` churn would
  only delay megamorphic fallback and increase scan cost.

Interpretation:

- correctness is the decisive result for this slice: js262 has zero
  regressions with the IC enabled;
- full-suite benchmark impact is effectively neutral in a one-run sweep after
  the offset check fix;
- focused property-heavy medians are slightly positive after the offset check
  fix;
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

## P3: Callsite Inline Cache for Compiled `a.b = v`

### Definition

A store callsite inline cache stores the existing own data-property slot for a
specific compiled named property write.

For:

```js
point.x = next;
```

the `point.x` store site can remember that a particular `TypeMap*` has own data
property `"x"` at a specific byte offset. On later executions, the site checks
the receiver shape pointer and writes the field directly.

This phase should start narrower than the load IC. It should optimize only
updates to an existing own data property on `MAP_KIND_PLAIN`. Property creation
and semantic edge cases stay on `js_property_set()` / `js_property_set_v()`.

### Scope

Apply the store IC only to compiled assignment/update sites where the property
name is fixed:

- include: `a.b = v`
- include: compound assignment after the read value is computed, such as
  `a.b += v`
- include: update assignment after the old value is computed, such as `a.b++`
- exclude initially: `a[b] = v`, `a?.b = v`, `super.b = v`, private fields,
  computed property names, destructuring stores, and class/static-field
  lowering paths
- preserve strict/sloppy behavior by falling back unless the fast path is a
  known successful own data-property write

The IC should replace only the final general named-property setter fallback in
assignment lowering. It must not change earlier special paths for globals,
module bindings, private fields, super properties, typed arrays, DOM/CSSOM
objects, or other exotics.

### Runtime structure

Use a separate fixed-size store IC structure. It can share the same entry shape
as the load IC, but keeping the type separate avoids mixing read and write
state while the semantics are being proven.

```cpp
#define JS_STORE_IC_POLY_MAX 4

typedef struct JsStoreICEntry {
    TypeMap* shape;
    ShapeEntry* entry;
    int64_t byte_offset;
} JsStoreICEntry;

typedef struct JsStoreIC {
    uint8_t state;      // empty, mono, poly, megamorphic
    uint8_t count;      // number of populated entries
    uint16_t miss_count;
    const char* name;   // interned/namepool property chars
    int name_len;
    Item key_item;      // optional cached string Item if lifetime/rooting is safe
    JsStoreICEntry entries[JS_STORE_IC_POLY_MAX];
} JsStoreIC;
```

Add a separate runtime flag, for example `LAMBDA_JS_STORE_IC=0`, so load and
store ICs can be measured and disabled independently.

### Runtime helper

Add a helper shaped like:

```cpp
extern "C" Item js_property_set_named_ic(
    Item object, const char* name, int name_len, Item value,
    int64_t strict, JsStoreIC* ic);
```

Fast hit checks after P0:

1. receiver is `LMD_TYPE_MAP`;
2. `map_kind == MAP_KIND_PLAIN`;
3. `m->data != NULL`;
4. `m->type == entry.shape`;
5. cached `ShapeEntry*` is non-null;
6. cached byte offset is in `m->data_cap`.

On hit, write `value` directly to the cached slot and return `value`.

The install/miss path performs the heavier validation before it installs or
uses a new entry:

- the receiver is a plain map;
- the shape entry exists as an own property before the write;
- the requested name matches the shape entry;
- `ShapeEntry::flags == 0`;
- byte offset is within `m->data_cap`.

Do not install for:

- absent properties or property creation;
- inherited properties, including inherited setters;
- accessors;
- deleted slots;
- descriptor-bearing maps;
- arrays and typed arrays;
- Proxy, DOM, CSSOM, ArrayBuffer, DataView, process.env, document proxy,
  iterator, array companion maps, or any other non-plain map kind;
- primitive wrappers and strings;
- any case that would throw or depend on strict/sloppy `[[Set]]` failure
  behavior.

The helper fallback remains the current setter:

- strict store: `js_property_set_v(object, key, value, 1)`;
- sloppy store: `js_property_set(object, key, value)`.

### IC state machine

Use the same empty / monomorphic / polymorphic / megamorphic state machine as
P2. A shape hit writes directly. A shape miss can install a new entry only if
the receiver already has an own plain data property with the fixed name. If the
site sees too many receiver shapes or too many semantic misses, mark it
megamorphic and skip probing.

### Invalidations and safety

The store IC should also be guard-based rather than mutation-invalidation
based.

Safe cases:

- ordinary writes to the same own data property keep the same shape and slot;
- adding/removing properties changes shape or promotes the map out of
  `MAP_KIND_PLAIN`, so the guard fails;
- descriptor mutation, accessor install, delete, freeze, seal, and
  prevent-extension paths must promote to `MAP_KIND_DESC` before the fast path
  can be trusted;
- strict/sloppy differences are irrelevant on a proven successful own data
  write, but every failing or ambiguous write must use the old setter.

Required reset is the same as P2: any IC storing raw `TypeMap*` or
`ShapeEntry*` must be scoped to the compiled-code lifetime or cleared when the
runtime heap/batch state is reset.

### MIR integration

In assignment lowering:

1. keep existing special cases first;
2. when the target is a non-computed named member write, allocate a per-site
   `JsStoreIC`;
3. emit `js_property_set_named_ic(obj, name_chars, name_len, value, strict,
   ic_ptr)`;
4. keep `js_property_set()` / `js_property_set_v()` as the helper slow path.

For compound/update assignments, the existing read side can still use P2. P3
only replaces the final store after the new value is computed.

### Profiling counters

Add store-side counters separate from load counters:

- `store_ic_probe`
- `store_ic_hit_mono`
- `store_ic_hit_poly`
- `store_ic_miss`
- `store_ic_install_mono`
- `store_ic_install_poly`
- `store_ic_megamorphic`

For focused profiling, record the site label, state, hit/miss counts, and the
number of fallback stores caused by absent properties versus descriptor/exotic
cases.

## P4: Follow-Up IC Performance Improvements

The current load IC is helper-call based. That kept the first slice small and
safe, but the quiet-machine rerun shows the helper overhead can erase the slot
lookup savings. Before expecting broad wins, optimize the IC implementation
itself:

1. Inline the monomorphic fast guard in MIR.
   - Load `ic->state`, cached shape, and cached byte offset directly from the
     per-site IC.
   - Check receiver map, `MAP_KIND_PLAIN`, data pointer, shape pointer, and
     bounds in MIR.
   - Read or write the slot directly on hit.
   - Call the helper only on miss, empty/poly/megamorphic states, or ambiguous
     receiver cases.
2. Keep the polymorphic fast path small.
   - Inline only mono first if code size is a concern.
   - If poly is inlined, scan at most four entries and keep the miss branch
     compact.
3. Avoid probing megamorphic sites.
   - Once a site is marked megamorphic, skip the IC helper and call the old
     property path directly.
   - This avoids adding an extra helper call on sites the IC has already
     rejected.
4. Disable semantically uncacheable sites.
   - Use `miss_count` to mark a site megamorphic or uncacheable after repeated
     absent/inherited/descriptor/exotic misses with no installs.
   - Keep a distinct "uncacheable" state if useful, so shape-megamorphic sites
     can be separated from never-cacheable semantic sites.
5. Make IC enablement profile-aware.
   - Keep counters cheap enough for normal builds or compile them behind the
     existing profiler mode.
   - Disable or bypass sites with low hit rate, high miss count, or frequent
     descriptor/exotic misses.
6. Reduce slow-path key work.
   - Reuse the interned/namepool key item where lifetime and rooting are
     already proven.
   - Avoid rebuilding boxed string keys on every miss.
7. Split helper variants by known strictness and operation.
   - For store ICs, emit separate strict/sloppy helper calls or pass a constant
     strict flag that MIR can fold.
   - Keep the hot successful own-data write path free of strict/sloppy
     branching.
8. Use profiler output to choose targets.
   - Prioritize sites with high probe count, stable receiver shape, and high
     own-data hit rate.
   - Do not broaden ICs for inherited/accessor/builtin-heavy sites until the
     plain own-data path is a measured win.

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

### P3 acceptance

1. Implement helper-only store IC with profiler counters and
   `LAMBDA_JS_STORE_IC=0`.
2. Keep the old setter path intact.
3. Run focused smoke tests that cover:
   - existing own data-property update;
   - strict and sloppy existing own data-property update;
   - accessor setter;
   - inherited setter;
   - inherited read-only/non-writable property;
   - absent property creation;
   - non-extensible receiver with absent property;
   - `Object.defineProperty` data-to-accessor and writable-to-non-writable
     transitions;
   - delete after cache population;
   - object shape change after cache population;
   - Proxy and DOM/typed-array non-plain objects falling back.
4. Run JS gtests and js262.
5. Run focused property-write-heavy benchmarks with profiler counters.
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

Expected profile signal for P3:

- `property_set` / `property_set_v` call counts decrease at hot member-write
  sites;
- store IC hit counts dominate misses on stable write sites;
- fallback reasons show absent/inherited/accessor/exotic stores remain on the
  semantic path;
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

Keep P3 if:

- js262 has zero regressions;
- JS gtests pass;
- descriptor promotion is in place before store IC enablement;
- focused semantic tests cover strict/sloppy writes, accessors, inherited
  setters, absent properties, non-extensible receivers, deletes, descriptor
  transitions, shape changes, and exotics;
- store IC hit rates are high on stable property-write sites;
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

Revert or gate P3 if:

- it creates any js262 regression;
- it changes strict/sloppy `[[Set]]` behavior;
- it bypasses accessors, inherited setters, non-writable properties, or
  non-extensible failure behavior;
- any descriptor-bearing ordinary map remains incorrectly tagged
  `MAP_KIND_PLAIN`;
- it increases benchmark wall time despite reducing setter call counts;
- most hot store sites become megamorphic or miss-heavy.

## Open Questions

1. Should the next load-IC slice inline the monomorphic guard in MIR immediately,
   now that the helper-only version is neutral to slightly negative?
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
6. Should P3 share entry structs/state constants with P2, or stay separate until
   store semantics and profiling are stable?

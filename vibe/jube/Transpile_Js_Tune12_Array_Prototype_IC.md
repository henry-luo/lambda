# Transpile JS Tune12 Proposal: Array Named Properties and Prototype Constructor Shapes

Date: 2026-06-25
Status: P1 array named-property IC and P2 function-constructor shape cache implemented; P3-P5 remain proposals

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
   side table to avoid changing the broader string representation?
4. Should descriptor own-data IC be read-only first, or should a descriptor
   store IC be added in the same phase?
5. How narrow should the first call IC be: direct local calls only, or method
   calls with stable callee identity as well?

# JS MVP Lambda classes and inheritance

**Status:** basic class support implemented in source, 2026-10-08. The latest
tuning passes 54 benchmark output oracles and resolves the original performance
regressions. Broader semantic/GC gates remain open as detailed below.
Scope: [JS_MVP_Lmd §21](../jube/JS_MVP_Lmd.md#21-classes-and-inheritance-aligned-with-lambda).
Authority: **S1.11**, **S2.1.3v2**, **S2.1.4**, **S11.3.1v2**,
**S12.3.3v2**, **D2.6.6v3–D2.6.10**, **D3.4.3v5–D3.4.5**,
**D5.3**, **D6.2.2v2**. This implements the existing shared-runtime design;
it does not revise the formal rulings.

## Implemented boundary

Named top-level classes support constructors, instance/static methods, a
single base class, default derived constructors, `super()`/`super.method()`,
class `this`/`new.target`, and `instanceof` along fixed class-created links.
Class bindings retain TDZ and inner-name const behavior. Constructors preserve
the original new target across base calls; explicit object returns override
the receiver, and derived constructors reject primitive returns or uninitialized
`this`. Methods are unbound function values whose calls supply the receiver.

Instance data uses ordinary field assignment. Addition, retyping and deletion
retain nominal identity through the existing Map transition families. Own
reflection distinguishes inherited methods from own fields; prototype methods
and constructor metadata are nonenumerable. Prototype and constructor maps are
readable but cannot be mutated in this phase.

Uncaught `throw`, immediate `throw new Error(message)`, and primitive template
interpolation cover the AWFY diagnostic paths. Throws use the shared Lambda
error carrier and payload tracing. General Error objects, handlers, closures
over locals, class expressions/nested classes, fields/private/accessor/computed
members, ordinary-function construction, and `super` writes remain excluded.
Computed `super` keys and Error options are also rejected by admission.
`__proto__`, descriptors, proxies and arbitrary prototype mutation remain
excluded. Unsupported runtime combinations return explicit capability errors.

## Shared representation and ownership

- `TypeNominal::base` is the ancestry authority. The JS class record holds
  instance, prototype and constructor-property shapes, with no separate JS
  parent chain. Instances use `Map`/`TypeMap`, canonical name-pool strings,
  shared shape hash lookup, field storage, allocation and transitions. No VMap
  or full-LambdaJS runtime helper is introduced.
- A small `TypeNominalExtension` member callback carries guest lookup semantics.
  `lambda_object_member` checks it after own fields and before Lambda method
  binding. Lambda's bound methods and JS's explicit receivers remain distinct
  at the language boundary (**S1.11**, **D2.6.9v3**).
- Runtime nominal checks reuse `lambda_nominal_derives_from`. Different
  instance shapes keep the same class record. Prototype metadata has its own
  nominal record so it is not mistaken for an ordinary instance.
- Constructor, prototype and static-property values live in three registered
  program slots per class. Records and blueprints are evaluation-owned;
  function tails contain only program/home pointers. Function objects, Maps,
  arguments, receivers and new targets use precise roots (**D5.3**).
- Class units use a seven-argument boxed MIR entry carrying receiver/new target.
  Existing units retain their five-argument entry and original Function size;
  existing direct native calls retain their specialized signatures. The call
  bridge preserves pending scalar returns and their companion slot until the
  generated caller adopts them into its own scalar home.
- Cross-language instance storage/ancestry and guest member lookup share the
  runtime foundation. Script-level cross-language call adapters and subclass
  construction are **not implemented**. Foreign constructor ABIs and foreign
  class bases are rejected before invocation; inherited Lambda field contracts
  cannot be bypassed by JS construction. Two-way interop remains an acceptance
  requirement for the later adapter work, not a completed claim.

## Helper disclosure inventory

The class allocation, receiver-call, metadata lookup, `super`, ancestry and
throw helpers were disclosed before implementation. Compiler utilities reuse
existing AST traversal, argument snapshots, MIR calls and field planning.

| Helper | Purpose / reuse |
|---|---|
| `mvp_lmd_class_record` | Check the MVP callable ABI or nominal extension and retrieve class metadata. |
| `mvp_lmd_inherited_member` | Shared instance/static inheritance walk over `TypeNominal::base`, using `typemap_hash_lookup` and `map_shape_field_to_item`; also supplies the Lambda member extension. |
| `mvp_lmd_class_new` | Initialize preplanned Maps and function values in registered class slots; reuse `mvp_lmd_object_new`, `mvp_lmd_function_new`, `map_field_store` and canonical names. |
| `mvp_lmd_class_property` | Class-only property/reflection policy; delegate ordinary storage and nonclass receivers to existing MVP helpers. |
| `mvp_lmd_class_invoke` | Receiver/constructor bridge with precise roots; reuse shared allocation and MIR entries. |
| `mvp_lmd_class_super` | Resolve the lexical home class's base constructor/prototype/static properties. |
| `mvp_lmd_constructor_result` | Apply base/derived constructor return rules with existing failure carriers. |
| `mvp_lmd_instanceof` | Apply `lambda_nominal_derives_from` to fixed class-created links. |
| `mvp_lmd_throw` | Allocate a shared `LambdaError`; retain a thrown scalar in destination-owned storage using `lambda_item_adopt_scalar_home`. |
| `class_plan` | Compiler class record, blueprint and root-slot planning. |
| `plan_shape_field` | Extracted common literal/class field planning, with a nonenumerable flag parameter; no duplicate layout algorithm. |
| `property_access` | Emit class dispatch only in class units; preserve original helper names and signatures elsewhere. |
| `reference_name` | Compiler-only deferred conversion of primitive numeric indexes; reuse existing canonical key conversion on the object fallback. Disclosed before implementation. |
| `type_tree_delete_field` | Shared bounded deletion-plan lookup/replay; reuse `TypeMapRetypePlan`, `type_tree_root_like`, field transitions and payload compatibility. Disclosed before implementation. |

Runtime imports explicitly describe GC, reentry, argument rooting and pending
scalar returns. `LmdCallCapture` was extended to reuse the ordinary call emitter
for `new` and `super`; primitive interpolation reuses existing conversions and
concatenation rather than adding another string helper.

## Performance isolation and measurements

The original 48 workloads use unchanged sources and a frozen Result6 release
control. Runs pin native MIR and compare self-reported execution time, excluding
source compilation/process startup. Alternating A/B order and an identical
control peer detect ordering noise. Every measured/discarded run checks its
output; discarded warmups capture fresh finalized MIR.

An initial implementation added class checks to ordinary property helpers.
Those checks were removed: class dispatch is now selected by the compiler only
for class units. Additional class lookup sites in the ordinary object source
file also changed Clang's helper outlining. Moving class runtime operations to
`mvp_lmd_runtime_classes.cpp` restores the prior ordinary helper sizes and
initially leaves `mvp_lmd_objects.cpp` identical to the starting source.

Before the ownership follow-up, residual performance shifts remained. MIR for deletion,
escaped retyping, gcbench and mbrot preserves the operation stream; differences
are embedded addresses. Link maps and disassembly of the shared Map set/delete,
allocation and MVP delete helpers show unchanged operations with relocated
calls/data. This is evidence of layout sensitivity, but does not by itself
establish the hardware cause or satisfy the no-regression requirement.
No padding, linker-order workaround or benchmark-specific path is shipped.

The follow-up reduces native mutation overhead using the existing borrowed
argument contract (**D5.1.3**, **D5.3.3**). `mvp_lmd_property_set` and
`mvp_lmd_property_delete` have only audited MIR callers or the class dispatcher,
which forwards the same rooted arguments. They create no intermediate GC
values before calling the shared shape functions and never reenter JS.
Their extra native root frames are therefore redundant. Both now borrow the
caller's roots; set also returns the original caller-owned scalar instead of
copying it into a temporary native-stack home. Import classifications and
caller safepoint roots remain intact. No new helper is added. This applies to
all admitted ordinary/class mutations and reduces the work on the two affected
paths; it does not establish a hardware explanation for the earlier layout
shifts.

### Initial class release comparison

`final4/comparison.json` records 15 alternating rounds over all **48** prior
workloads: **2,160 measured / 144 discarded** output checks pass. The geometric
mean elapsed-time ratio is **0.9962** (0.38% lower); no individual two-sided 95%
interval in this screen lies wholly above one. Longer **60-pair** checks
(`confirm4/comparison.json`, **720 measured / 12 discarded** matching outputs)
identify two remaining regressions:

| Workload | Control → candidate ms | Change in time | Candidate/control 95% interval |
|---|---:|---:|---:|
| object_retype_escaped | 6.7820 → 5.6665 | −16.45% | 0.830–0.841 |
| object_delete | 2.3525 → 2.5035 | +6.42% | 1.035–1.092 |
| map_lookup | 13.1600 → 13.2580 | +0.74% | 1.003–1.014 |
| json_gen | 5.7075 → 5.6885 | −0.33% | 0.992–1.004 |

All four identical-control peer intervals span one. Deletion's wide interval
shows substantial variability but does not erase the measured regression.
Native helper relocation is a plausible contributor; it remains an attribution
limit, not a performance waiver. **The no-regression acceptance condition is
not yet satisfied.** No fresh untyped-Lambda/full-LJS comparison is claimed.

### Six newly enabled workloads

The AWFY benchmark declarations, kernels, input sizes, iteration counts and
`verifyResult` checks are retained. Temp adapters replace the Node host timing
harness with `innerBenchmarkLoop(...) ? 1 : 0`. Both lanes include class creation
and setup in the timing interval; initial source compilation is excluded, while
Node lazy compilation/tiering during execution is included. These fresh-process
medians are not steady-state Node peak throughput.

All **180 measured / 12 discarded** output checks pass, with 15 samples per
lane, Node **v22.13.0** and the release candidate described below.

| AWFY workload | MVP ms | Node ms | MVP / Node |
|---|---:|---:|---:|
| sieve | 0.599 | 0.388 | 1.54× |
| permute | 3.231 | 0.790 | 4.09× |
| queens | 1.547 | 0.602 | 2.57× |
| towers | 4.450 | 1.180 | 3.77× |
| list | 0.797 | 0.395 | 2.02× |
| mandelbrot | 32.100 | 31.658 | 1.01× |

### Provenance and remaining validation

Artifacts are under `temp/mvp_classes_20261008/`: exact release binaries,
paired runners, source hashes, MIR dumps, sample outputs, link maps and build
logs. Existing Result2–6 history is unchanged.

- Control: `control.exe`, SHA-256
  `da89d81a9fa893516ede62345771b82a3709fd45faed23c0c2bc7dc37f7220ca`.
- Candidate: `final4.exe`, SHA-256
  `bd6d08853be061a1af965dd933e2056d00f318e9aaa494bb7724dc8cd263e0d1`.
- Six-target evidence: `new_classes_final4/comparison.json`, `class_manifest.json`
  and `class_bench.py`.
- `make build-release-compile` succeeds; `build11.log` records the final build.
  No unit tests were added or run in this turn. Earlier §20 baseline totals
  apply to its prior binary, not this class implementation.

The broader semantic/forced-GC matrix and Lambda/Test262 baselines remain
pending. In particular, it must cover inherited/overridden static methods,
detached methods, constructor return/TDZ/super edge cases, class-name bindings,
reflection after shape transitions, scalar and receiver ownership across GC,
uncaught error payloads, and two-way instance/call/subclass adapters when those
adapters are introduced.

```sh
python3 temp/mvp_followup_20261008/runner.py \
  --candidate temp/mvp_classes_20261008/final4.exe \
  --control temp/mvp_classes_20261008/control.exe \
  --output temp/mvp_classes_20261008/replay --runs 15
python3 temp/mvp_classes_20261008/class_bench.py
```

## Class and regression tuning, 2026-10-08

### Changes and ownership

- Generic indexed references preserve numeric keys until an object needs their
  string spelling. Runtime-unknown nonnumeric keys still convert before the
  assignment RHS, preserving conversion errors and evaluation order. Array and
  typed-array branches reuse their existing index checks; object fallbacks
  retain canonical UTF-8 keys and the `__proto__` exclusion.
- Fixed-name class reads and writes have one cache per compiled site, guarded
  by the exact immutable shape. Own-field hits reload the current payload;
  same-type writes reuse `map_field_store`. Inherited method values remain
  reachable from permanent class root slots. Shape changes and private shapes
  take the ordinary dispatcher, preserving own overrides and metadata write
  rejection (**D3.4.3v5–D3.4.5**).
- Cache hits reuse `map_shape_field_to_item` and `map_field_store` as non-GC
  imports. Borrowed field scalars still enter the caller's scalar home before
  a safepoint. Ordinary methods use the existing seven-argument MIR entry after
  checking the extended carrier, runtime context and non-constructor flag.
  Construction retains the native bridge; pending scalar returns and the
  companion slot retain the existing caller-consumption protocol
  (**D5.1.3**, **D5.2.1v3**, **D5.3.3**, **D6.2.2v2**).
- `type_tree_delete_field` moves the existing deletion replay into a bounded
  cache on immutable tree-owned shapes. Deletion and retyping share plan
  storage and the payload migration routine; private fallback behavior and
  nominal family identity remain unchanged. Retype plans also retain their
  shared resolver's storage classification, avoiding
  repeated classification on every hit (**D3.4.5**).
- Ordered Maps cache an entry ordinal, with no additional strong GC edge.
  Every reuse checks bounds, liveness and SameValueZero key equality; deletion
  still finds its predecessor. Clear and compaction cannot make a stale
  ordinal a false hit. Only existing-entry hits seed the cache, avoiding a
  previous-key comparison for bulk unique insertion. Packed integer equality
  avoids unnecessary double conversion. The C GC layout and C++ layout
  assertions include the new scalar field (**D5.3**).

### Measurement boundary

Artifacts are under `temp/mvp_class_tuning_20261008/`. The fresh source-control
release was built before edits; the original pre-class release is retained for
the original regression checks. All timing uses release binaries, pinned
native MIR, alternating fresh processes, self-reported workload time, output
oracles and an identical-control peer. One discarded run per lane captures
fresh MIR. Startup and initial compilation are excluded.

Class timing includes class initialization and setup; Lambda uses each port's
existing `benchmark()` clock interval. Workload sizes are unchanged. Towers
uses linked class instances and disk-order checks in JS versus array stacks in
Lambda; both make 8,191 moves. Queens solves ten boards in both ports, with an
additional final-board check in Lambda. These ratios compare the existing
ports, not identical storage algorithms.

Early screens found small new costs in escaped retyping and Map iteration.
Longer paired runs guided the cached storage classification and existing-hit
Map cache policy above; earlier candidate artifacts are retained as evidence.
Class semantic/forced-GC and Lambda/Test262 baseline suites have not been run
for this tuning round. Benchmark output checks do not close those gates.

### Latest measured results

`final6-classes/comparison.json`: 15 alternating rounds, **360 measured / 24
discarded** matching outputs. Self-reported medians in milliseconds:

| Workload | Fresh control | Tuned MVP | Speedup | Untyped Lambda | MVP / Lambda |
|---|---:|---:|---:|---:|---:|
| sieve | 0.600 | 0.093 | 6.45× | 0.027 | 3.44× |
| permute | 3.214 | 0.812 | 3.96× | 0.281 | 2.89× |
| queens | 1.563 | 0.440 | 3.55× | 0.178 | 2.47× |
| towers | 4.510 | 1.697 | 2.66× | 0.494 | 3.44× |
| list | 0.816 | 0.402 | 2.03× | 0.546 | 0.74× |
| mandelbrot | 32.359 | 32.237 | 1.00× | 37.846 | 0.85× |

`final6-48/comparison.json`: all **48 prior workloads** pass, **2,160 measured /
144 discarded** outputs. Geometric-mean candidate/control time is **0.96770**.
The screen's small slowdowns and noisy larger point estimates receive 60-pair
follow-up in `confirm6-fresh` and `confirm6-noise` (**1,800 measured / 30
discarded** outputs). No candidate interval lies wholly above one. In particular:

| Fresh-control confirmation | Candidate/control 95% interval |
|---|---:|
| object_retype_escaped | 0.993–1.011 |
| map_iteration | 0.997–1.000 |
| gcbench | 0.997–1.004 |
| binarytrees | 0.991–1.003 |

These intervals do not prove exact parity; sub-percent effects remain hard to
resolve. Spectralnorm was noisy even in the identical-control lane; its
confirmation interval is 0.912–1.081, so no small-effect conclusion is claimed.

`confirm6-old/comparison.json`: 60 pairs against the original pre-class release,
**720 measured / 12 discarded** matching outputs:

| Workload | Original → tuned ms | Time change | Candidate/control 95% interval |
|---|---:|---:|---:|
| object_retype_escaped | 6.7980 → 5.8015 | −14.66% | 0.846–0.859 |
| object_delete | 2.3560 → 1.1890 | −49.53% | 0.485–0.519 |
| map_lookup | 13.0985 → 8.3255 | −36.44% | 0.630–0.639 |
| json_gen | 5.7110 → 5.6990 | −0.21% | 0.993–1.009 |

The two original regressions are resolved. Retained changes reduce repeated
conversions, dispatch, hashing and shape analysis. The intermediate candidates
also showed allocation sensitivity despite identical normalized MIR; its
hardware/compiler cause is not established. The extra cached type pointer and
direct allocator-call experiment were removed because they did not establish
a useful improvement. The retained type key/storage bytes fit the existing
plan padding. No padding or linker-order workaround is added.

### Latest provenance

- Host: macOS 26.5.2, ARM64.
- Fresh control: `control.exe`, source HEAD
  `670733b89` (full revision in `final-provenance.json`), SHA-256
  `eee3d7c85c67bdb79674cb5df281aa8148dd1187b66bf0eaddddb9f8843de128`.
- Tuned release: `final6.exe`, SHA-256
  `0b6688bc8b5a6742207b23d3b19e6ec42dedbc4ad4fb45227f2ba940efafb037`.
- Original regression control: `../mvp_classes_20261008/control.exe`, SHA-256
  `da89d81a9fa893516ede62345771b82a3709fd45faed23c0c2bc7dc37f7220ca`.
- `final6-build.log` records the successful release build. `final-provenance.json`
  and `final-source.patch` retain source hashes and the complete change.
- Runners are `classes.py` and `../mvp_followup_20261008/runner.py`; each result
  records exact commands, flags, binaries, inputs, MIR, samples and output
  checks. The exploratory `allocation-ablation` run overlapped a build and is
  excluded from acceptance evidence.
- The accepted 54-workload capture and separate confirmations are published as
  [MVP_Result7](../../test/benchmark/js_mvp_lmd/MVP_Result7.md), with all five raw
  captures embedded in its JSON. The rolling series retains Result3–7; Result2
  has an exact local backup recorded in the series index.
- No unit, forced-GC, Lambda baseline or Test262 suite was added/run for this
  tuning round.

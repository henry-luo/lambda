# JS Tune15 — Repair constructor regressions, then close the QuickJS gap

**Version:** 1.0.0

**Date:** 2026-09-22

**Status:** PROPOSED — diagnosis and release experiments recorded; no runtime fix
or performance-goal acceptance is claimed.

**Audited tree:** `10fddc11245fd0322d489aa7013b61ee32501669`; Result48's
executable is the release archive from `0e6f8abd892734614123d70e079504a2c34f8016`.
This proposal follows [JS Tune14](JS_Tune14.md), not a completed Tune14 rollout.
`T15-*` identifiers are work packages, not new normative rulings.

## 1. Decision and scope

Tune14 made real progress on numeric/array workloads, but did not meet its
QuickJS milestone. Its last frozen intermediate control, C14, was **1.120086×
QuickJS** by workload-time geometric mean. Result48 is **1.200379×** and wins
only **23/63** rows. Its time sum is **1.696643× QuickJS**. The immediate
priority is an avoidable constructor/property-path regression, followed by
broader improvements to ordinary object operations and numeric-array loops.

The strongest new evidence is a same-binary source ablation: disabling class
constructor-shape admission with a semantically inert statement reduces CD
from **18.060 s to 3.628 s**, and Havlak from **48.686 s to 12.914 s**, while
their result checks still pass. Source inspection identifies a reserved-field
initialization mismatch that sends these objects through generic descriptor
construction. This is a much stronger diagnosis than attributing the loss to
general JIT overhead or GC alone; the fresh samples in §3.3 independently show
the expensive descriptor path.

The plan is:

1. Repair the reserved-field initialization path and prove which generated
   guards actually succeed after construction.
2. Recover object-heavy regressions before expanding constructor/alias coverage.
3. Reduce repeated ordinary property/descriptor/name work across several losing
   rows, and complete bounded Number/array producer-to-consumer paths in search.
4. Accept the result only with fresh complete matrices, paired regression gates,
   non-vacuous correctness checks, and final runtime baselines.

The target is the existing **full LambdaJS MIR** execution lane, not the
restricted MVP, a benchmark rewrite, an AST substitution, a private heap
replacement, or restoration of a removed back end. No production runtime,
canonical benchmark, formal ruling, vendor source, or build configuration is
changed by this analysis.

### 1.1 Normative constraints

The [formal design](../../doc/Lambda_Formal_Design.md) and
[formal semantics](../../doc/Lambda_Formal_Semantics.md) take precedence over
older tuning proposals:

| Ruling | Consequence for Tune15 |
|---|---|
| **S1.11** | Hosted JavaScript retains ECMAScript semantics. No weakened property, Number, string, exception, or evaluation-order contract. |
| **D1.3v3**, **D8.2.3–D8.2.6** | Reuse applicable shared storage, indexed facts, `MirValue`, and physical lowering. Extract common machinery after two real clients converge; do not invent a parallel optimizer. |
| **D8.1.3v12** | Preserve the current selected JS backend and shared runtime ownership. Tune14's older v11 references do not override the current ruling. |
| **D8.4.1v2** | No mutable inline caches, feedback vectors, or callee/method caches. Use immutable compile-predicted guards with the shared semantic kernel on a miss. Ordinary TypeMap metadata is not per-site feedback. |
| **D1.4v4**, **D8.4.3v2** | Preserve explicit completions and cleanup through every frame. |
| **D1.5v2**, **D5.3.1–D5.3.5** | Precise GC and current roots remain mandatory. Never substitute conservative stack scanning or remove roots based only on benchmark survival. |
| **D8.6.1–D8.6.3** | Finalized MIR, zero-slack budgets, and independent dynamic rooting oracles remain acceptance gates. |

## 2. What Result47 can, and cannot, establish

### 2.1 Chronology and measurement identity

**Result47 predates Tune14.** It is the motivation for Tune14, not a measurement
of Tune14's final effectiveness. The relevant sequence is:

| Record | Date | Interpretation |
|---|---|---|
| [Result46](../../test/benchmark/benchmark_results_v46.json) | Sep 17 | Historical full matrix including QuickJS; its Navier result later fails the stronger oracle. |
| [Result47](../../test/benchmark/benchmark_results_v47.json) | Sep 18 | No QuickJS run. Twelve repaired cells were merged from a later binary; original and repair executables must stay distinct. |
| [Tune14 C14](../../test/benchmark/js_mvp/tune14/c14_session1.json) | Sep 21 | Frozen intermediate release, same-session QuickJS/Node controls; not the final Tune14 tree. |
| [Result48](../../test/benchmark/benchmark_results_v48.json) | Sep 22 | Current full release matrix with QuickJS; includes later constructor/alias/call changes. |

The selected JavaScript benchmark sources did not change between the R47 and
R48 archive commits; the changed hyphen generator and Lambda-side table are
not the selected self-contained `hyphen.js` workload. The current Tune14
manifest verifies all 63 workloads. All **65 distinct selected source/input
files** are byte-identical between those archive commits. Wrappers, oracle
strength, historical repairs, and host/session effects still prevent treating sequential snapshots
as isolated compiler experiments.

### 2.2 Aggregate accounting

For the same 63 row identities, with lower ratios better:

```text
G(A/B) = exp(mean(log(time_A_i / time_B_i)))
T(A/B) = sum(time_A_i) / sum(time_B_i)
```

These are workload execution times, not startup/JIT-inclusive wall times.
`T` is not the arithmetic mean of row ratios. All calculations and all 63 row
deltas are reproducible in
[snapshot_analysis.json](../../test/benchmark/js_tune15/snapshot_analysis.json).

| Snapshot | JS time sum | G(JS/QuickJS) | T(JS/QuickJS) | JS wins vs QuickJS | G(JS/Node) |
|---|---:|---:|---:|---:|---:|
| R46 | 163.162 s | 1.464372 | 1.442349 | 17/63 | 9.995931 |
| R47 | 135.315 s | unavailable | unavailable | unavailable | 9.725718 |
| C14 | 124.236 s | 1.120086 | 1.221277 | 21/63 | 8.094955 |
| R48 | 172.317 s | 1.200379 | 1.696643 | 23/63 | 8.646696 |

- R47/R46: raw JS geomean **0.786940**, but Node also improves to **0.808804**.
  The Node-normalized sensitivity ratio is **0.972968**, only about 2.7% better.
  It is a control-engine sensitivity check, not a universal correction factor.
- R48/R47: JS geomean **0.889383** with essentially unchanged Node
  (**1.000369**), but the JS sum is **1.273455**. Thus “11.1% better” describes
  one aggregate only; the summed workload time is **27.3% worse**.
- R48/C14: JS geomean **1.071271**, Node **1.002914**, JS sum **1.387015**.
  Later changes lost ground relative to the actual Tune14 control.
- R48's startup-inclusive `G(JS_e2e/QuickJS_e2e)` is **2.725210**. Beating the
  workload-time target alone would not establish a cold-start advantage.

Using R47 JS with R46 QuickJS gives a **cross-session proxy**, not a measured
Result47 QuickJS ratio. Neither that proxy nor a restricted historical MVP
result is an acceptance control.

### 2.3 Result47's apparent regressions

These are historical observations, not newly isolated causal findings:

| Row | R46 JS ms | R47 JS ms | JS ratio | Node ratio | Disposition |
|---|---:|---:|---:|---:|---|
| `jetstream/navier_stokes` | 213.933 | 565.384 | 2.643 | 1.007 | R46 fails the stronger semantic oracle; not a valid recovery target. |
| `jetstream/splay` | 324.101 | 374.476 | 1.155 | 0.993 | Credible timing candidate; exact-archive replay and allocation/property attribution still needed. |
| `jetstream/hashmap` | 1,435.002 | 1,553.601 | 1.083 | 0.989 | Historical loss precedes the much larger R48 constructor loss. Do not conflate them. |
| `text/log_pipeline` | 13,620.518 | 14,292.033 | 1.049 | 0.999 | Candidate for text/object profiling, not proven code attribution. |
| `jetstream/raytrace3d` | 546.087 | 569.671 | 1.043 | 1.018 | Small relative loss; paired evidence required. |
| `r7rs/ack` | 13.718 | 14.267 | 1.040 | 0.880 | Host/control difference substantial; no causal claim. |
| `r7rs/fib` | 1.791 | 1.855 | 1.035 | 0.862 | Small timing and host sensitivity; no causal claim. |

The important correction is Navier. Tune14's archive audit (§2.4) found that
**R46 and the R47 repair binary fail** the frame-15/full-density oracle, while
the **original R47 and C14 pass**. The old 31-pair 2.646× replay compared equal
empty stdout from the one-frame timing wrapper; that was not sufficient
semantic evidence. Tune14 traced an indexed-write Reference lifetime bug and
repaired it, then obtained an oracle-backed dense-companion-read speedup.
Do not restore invalid old lowering to reproduce 214 ms. Preserve the valid
current ~184 ms result and its stronger oracle.

## 3. The regressions that now dominate

### 3.1 Current losses and controls

| Row | R47 JS ms | C14 JS ms | R48 JS ms | R48/R47 | Node R48/R47 | R48 JS/QuickJS |
|---|---:|---:|---:|---:|---:|---:|
| `awfy/cd` | 3,807.176 | 3,687.588 | 18,107.119 | 4.756 | 1.015 | 18.825 |
| `awfy/havlak` | 14,336.525 | 13,869.436 | 48,537.559 | 3.386 | 1.091 | 14.866 |
| `jetstream/hashmap` | 1,553.601 | 1,449.025 | 3,456.255 | 2.225 | 0.997 | 10.978 |
| `awfy/deltablue` | 432.532 | 396.282 | 890.337 | 2.058 | 0.998 | 9.011 |
| `awfy/mandelbrot` | 50.265 | 79.585 | 79.687 | 1.585 | 1.002 | 0.091 |
| `awfy/bounce` | 3.871 | 3.568 | 6.099 | 1.576 | 1.021 | 6.970 |
| `awfy/json` | 42.436 | 39.207 | 58.594 | 1.381 | 0.975 | 5.455 |
| `awfy/towers` | 14.448 | 13.842 | 18.144 | 1.256 | 1.018 | 8.096 |

CD, Havlak, HashMap and DeltaBlue together rise from **19.402 s in C14 to
70.991 s in R48**, now **41.2%** of the JS time sum. Mandelbrot was already
slower in C14, so it is not automatically the same late-constructor problem;
remaining much faster than QuickJS does not excuse its relative loss.

The new exact R47/R48 release replay gives:

| Workload | Pairs | R47 median ms | R48 median ms | R48/R47 | Output |
|---|---:|---:|---:|---:|---|
| CD | 11 | 3,803.692 | 18,010.706 | **4.735059** | all equal; CD result marker |
| HashMap | 11 | 1,552.058 | 3,442.322 | **2.217908** | all equal; source result/keySum/valueSum assertions |

R48 loses every pair on both rows. Node's near-flat snapshot is corroborating
context; the interleaved exact-archive replay is the direct timing evidence.
HashMap's normalized stdout is empty in this canonical wrapper, so its built-in
assertions must be retained; empty-output equality alone is not the oracle.
Raw pairs, executable/source hashes, ordering and bootstrap bounds are in
[r47_r48_cd_hashmap.json](../../test/benchmark/js_tune15/r47_r48_cd_hashmap.json).

### 3.2 Same-binary constructor ablation

The R48 binary was used for **both** sides. Only diagnostic copies under
`temp/tune15/probes/` changed:

- CD: insert `0;` at the entry of each of its 19 class constructors.
- Havlak: the same insertion in 13 class constructors.
- HashMap: insert `this;` at the entry of plain `Entry` and `HashMap`
  constructors. A receiver-observing prelude declines the plain-function
  recipe, while the read has no observable effect in these base constructors.
  Both sides retain the canonical wrapper and its three result assertions,
  followed by an explicit post-timing `HashMap assertions: PASS` marker.
- A numeric no-op is safe even before `super()` in derived constructors. The
  class shape planner requires directives/direct field assignments and declines
  this statement; runtime workload, loop counts and verification are unchanged.

| Workload | Pairs | Original ms | Recipe-disabled ms | Candidate/control | Oracle |
|---|---:|---:|---:|---:|---|
| CD | 3 | 18,059.884 | 3,627.996 | **0.200887** | `CD: PASS` on every run |
| Havlak | 3 | 48,686.279 | 12,913.573 | **0.265240** | `Havlak: PASS` on every run |
| HashMap | 11 | 3,453.733 | 1,450.650 | **0.420024** | all three source assertions and explicit PASS marker |
| DeltaBlue | 11 | 889.666 | 378.567 | **0.425515** | `DeltaBlue: PASS` on every run |
| Bounce | 11 | 6.141 | 3.604 | **0.586779** | `Bounce: PASS` on every run |
| JSON | 11 | 59.199 | 37.948 | **0.641027** | `Json: PASS` on every run |
| Towers | 11 | 18.211 | 13.119 | **0.720392** | `Towers: PASS` on every run |

All pairs favor disabling the recipe, with matching output and explicit PASS
markers. See
[constructor_ablation.json](../../test/benchmark/js_tune15/constructor_ablation.json)
and [hashmap_ablation.json](../../test/benchmark/js_tune15/hashmap_ablation.json).
The smaller AWFY follow-up used the identical class no-op transformation in
17/8/15/2 constructors respectively; all **44/44 pairs** favor it and pass
their explicit checks. See
[small_constructor_ablation.json](../../test/benchmark/js_tune15/small_constructor_ablation.json).
These are **diagnostic source ablations**, not a production fix or a final
11/31-pair compiler-patch acceptance. They change admission and generated code
together; they do not apportion the loss between initialization, shape changes,
miss guards, allocation and GC. They nevertheless localize most of these two
large class-constructor losses, and the HashMap plain-constructor loss, to the
recipe path rather than unavoidable full-JS overhead.

### 3.3 Source-level mechanism: reserved storage is not an existing property

The relevant chain is visible in the audited source:

```text
dynamic this.field = rhs admitted by constructor planner
  -> reserve a fixed slot (often NULL-typed), not yet an observable property
  -> source assignment via js_set_name_id
      -> same-slot lookup rejects RESERVED (semantically correct)
      -> ordinary-add helper sees raw ShapeEntry and says "not an add"
      -> generic OrdinarySet / descriptor / prototype machinery
      -> construct a receiver descriptor; Reflect.defineProperty path
      -> publish field, detach/transition shape as required
  -> later predicted exact-original-shape guards can miss
```

Evidence and ownership:

1. `jm_constructor_shape_field_type` in
   [js_mir_expression_lowering.cpp](../../lambda/js/js_mir_expression_lowering.cpp)
   admits safe nonliteral RHS expressions with `LMD_TYPE_NULL` as a placeholder.
   `jm_append_straight_line_constructor_shape_fields` and
   `jm_append_plain_constructor_shape_prefix` extend the admitted population.
2. `js_new_literal_object_with_typemap` in
   [js_runtime.cpp](../../lambda/js/js_runtime.cpp) reserves per-instance slots.
   `js_named_fast_lookup` explicitly rejects a still-reserved field with
   `JS_OPT_REASON_NAMED_FAST_RESERVED`, as it must for observable property reads.
3. `js_ordinary_add_own_data_property` in
   [js_globals.cpp](../../lambda/js/js_globals.cpp) rejects **any raw shape hit**
   as an existing own slot. It does not distinguish a reserved, still-absent
   slot. Thus both fast choices refuse the constructor's first source store.
4. `js_set_completion_with_key` then performs the general property algorithm.
   `js_reflect_set_define_receiver` allocates a descriptor through
   `js_make_reflect_set_value_desc`, including `value`, `writable`, `enumerable`
   and `configurable`, before calling `js_reflect_define_property`. This is
   semantically defensible but expensive work for a proven ordinary first store.
5. `js_typemap_transition_for_type` in
   [js_property_attrs.cpp](../../lambda/js/js_property_attrs.cpp) changes the
   object's shape when the placeholder acquires its actual type. Meanwhile,
   `jm_emit_predicted_literal_field_data_guard` requires the **original exact
   shape**, then tests data/capacity and calls an initialized-slot helper.
   Dynamic NULL fields are not themselves admitted as typed field reads.
   Admission therefore does not prove useful post-construction specialization.

The storage half already has a relevant implementation:
`js_create_data_property_initialize_reserved_literal_slot` avoids descriptor
detachment for reserved **object-literal** writes using `fn_map_set`. Reuse
its proven storage mechanics after factoring an appropriate common leaf;
do not duplicate it. **Do not simply use CreateDataProperty for assignment**:
`this.x = v` must still honor inherited setters/non-writable properties,
extensibility, receiver identity, strict failure and exception behavior.
That boundary is required by **S1.11** and **D8.4.1v2**.

The pre-existing `temp/tune14/havlak_current.sample.txt` supports this cost
model: named lookup, descriptor operations, hashing, realm metadata and shape
lookup dominate its sampled runtime work. It is a historical diagnostic,
not a hash-identified R48 profile or an allocation census; it cannot assign
exact percentages to the R48 slowdown.

**Fresh R48 sampling corroborates the mechanism.** Two separate five-second,
one-millisecond-interval `/usr/bin/sample` captures started about one second
after launching original and recipe-disabled Havlak on the same R48 archive.
Both completed with `Havlak: PASS`. Sandbox attachment initially failed;
rerunning the owned diagnostic processes with approved permission succeeded.
These sampled runs are not included in the timing pairs above.

| Named frame | Original inclusive stack count | Recipe-disabled inclusive stack count |
|---|---:|---:|
| `js_set_completion_with_key` | 2,767 | 34 |
| `js_reflect_define_property` | 1,674 | 0 |
| `js_object_define_property` | 1,631 | 0 |
| `js_descriptor_from_object` | 671 | 0 |
| `js_ordinary_add_own_data_property` | 78 | 751 |

The original tree explicitly contains constructor → `js_set_name_id` →
`js_set_key_strict_policy` → `js_set_completion_with_key` →
`js_reflect_define_property` → `js_object_define_property` → descriptor
decoding. The disabled recipe instead reaches the cheap ordinary-add kernel.
That is the runtime behavior predicted by the source audit.

Counts sum named frames in the call-graph section, not function invocations;
nested/recursive inclusive samples must not be added as disjoint time. Worker
sampling windows contain 3,858 and 3,861 observations, and the faster run is at
a different workload phase. Zeros mean **not sampled**, not globally absent.
These short profiles establish path use, not whole-run allocation counts,
guard-hit rates or precise percentages of the speedup. Raw compressed captures:
[original](../../test/benchmark/js_tune15/havlak_original.sample.txt.gz),
[recipe-disabled](../../test/benchmark/js_tune15/havlak_no_ctor_recipe.sample.txt.gz).

### 3.4 What remains unproved

- Constructor admission is strongly implicated for CD/Havlak/HashMap by same-binary
  ablation, but the exact shares of slow initialization, shape detachment,
  persistent read misses and GC require dynamic counters or an isolated patch.
- DeltaBlue/bounce/json/towers independently improve under the same recipe
  ablation. An isolated compiler/runtime repair must still establish the
  mechanism's contribution and its safe production form.
- The strict-receiver `mir_this` slice is not a demonstrated cause: Tune14's
  11-pair experiment was inconclusive, and its predecessor was already at
  roughly 49 seconds on Havlak. Do not blame the latest call-entry commit just
  because it is nearby in history.
- Mandelbrot/mbrot/puzzle losses exist before the late object changes. Replay
  their exact predecessors separately; code size, integer/Number conversions,
  register pressure and initialization checks are hypotheses, not findings.
- The R46→R47 splay/HashMap/text losses need separate historical bisection if
  still material after current repairs. This proposal does not invent causes
  for them from snapshot ratios.

## 4. Why Tune14 did not cross the target

### 4.1 Useful slices were implemented, not the whole proposed optimization

R47→R48 observations include `diviter` **12,688.846→613.697 ms**, quicksort
**133.237→18.008 ms**, Navier **565.384→183.963 ms**, and levenshtein
**218.056→82.348 ms**. Tune14 also records accepted isolated quicksort, FFT,
Navier, literal-return and closure-source improvements. These gains should be
preserved, not discarded wholesale with the regressed constructor admission.

But the proposal's complete native-region objective was not achieved across
common operations. A raw parameter or specialized load can still be followed
by boxed comparisons, generic updates, repeated metadata lookup, root
publication and boxed results. The general region work was deferred; several
Number/store experiments were measured and rejected. That is a partial
implementation, not evidence that the underlying strategy cannot work.

### 4.2 Constructor coverage expanded without accepted performance evidence

Tune14 explicitly says its expanded constructor/alias paths have semantic and
forced-GC coverage **without accepted paired performance evidence**. C14 is
not the binary containing every later extension. The current result exposes
the missing performance gate: more admitted sites can mean more overhead if
their initialization takes the fallback and their exact-shape guards then miss.

`JS_OPT_MIR_LITERAL_FIELD_ADMITTED` is emitted during compilation. It counts
planned/emitted sites, **not runtime hits**. A static admission count or a MIR
guard's presence cannot establish that useful work ran on the fast arm. This
distinction must become explicit in every Tune15 acceptance record.

### 4.3 Workload sum and the QuickJS geometric mean need different priorities

R48 `text_search` consumes **55.227 s**, about **32.0%** of summed time, but it
is only one of 63 equally weighted rows. Halving it saves about **16.0% of the
sum** while improving the geomean by only **1.09%**. Search-first optimization
is useful, but cannot alone satisfy the equally weighted QuickJS goal.

R48 suite ratios show where the broad deficit remains:

| Suite | G(JS/QuickJS) |
|---|---:|
| r7rs | 0.156 |
| larceny | 0.608 |
| kostya | 0.988 |
| text | 2.137 |
| beng | 2.507 |
| jetstream | 2.578 |
| awfy | 3.443 |

Numeric recursion already supplies large wins. Object/data-processing and
ordinary collection operations lose on many rows. Representative R48 gaps
include revcomp **8.67×**, microdiff **7.75×**, Richards **5.08×**, base64
**3.08×**, and fast_diff **3.20×**. These ratios select investigation targets;
they do not by themselves identify each row's hot helper.

### 4.4 Quantitative improvement budget

At `G=1.200379`, crossing 1 requires a **16.69% reduction in geometric-mean JS
time**, not a 20% reduction. The log-ratio debt is `63*ln(G)=11.506129`.
Reaching the inherited 0.80 stretch target requires about **33.35%** reduction.

Pure arithmetic substitutions, holding every other R48 row and QuickJS fixed:

| Counterfactual, not forecast | Resulting G(JS/QuickJS) |
|---|---:|
| Restore CD/Havlak/HashMap to R47 timings | 1.134094 |
| Restore CD/Havlak/HashMap to C14 timings | 1.131672 |
| Also restore DeltaBlue to C14 | 1.117224 |
| Apply the seven measured source-ablation ratios to R48, all other rows unchanged | 1.092029 |

The last row is also only a sensitivity calculation: the diagnostic no-ops
are not a shipping solution, and no complete matrix was run on that combined
source variant. Even this substantial recovery would not cross 1.00.

Thus **repairing regressions is necessary but insufficient**. After the
four-row restoration, about **10.5%** geometric-mean time reduction still
remains. For scale, halving 12 distinct rows would multiply the overall
geomean by `2^(-12/63)`, taking 1.117224 to about 0.979. This is not a promise
of twelve 2× wins: it is a budget showing why a shared operation that helps
many rows can matter more to this milestone than one spectacular outlier.

## 5. Proposed implementation program

### T15-0 — Freeze a valid control and expose runtime fast-path use

Use the exact R48 release as the current repair control, C14 as the cumulative
Tune14 control, and original R47 only for historical attribution. Retain the
current 63-row manifest, expected outputs, generated wrapper contract, binary
hashes and QuickJS/Node versions. R47-repair is not interchangeable with R47.

Extend the **existing** `js_exec_profile` event system only where evidence is
missing. Collect separately from uninstrumented release timings:

- Constructor recipes/instances, reserved first stores, and why ordinary-add
  refuses them; initialized/default-data stores versus generic descriptor work.
- Runtime predicted-field probes, hits, shape misses, reserved misses and type
  misses; distinguish load from store and report hot function/site ownership.
- Shape transitions, private detachments and descriptor allocations per
  constructed instance. Attribute allocations/GC after locating their producer.
- Generic numeric/index helper traffic, Number versus coercing operands,
  packed/holey/companion arrays, and actual cold/miss-path frequency.

Inspect finalized MIR for the same hot functions. A counter-enabled build is
diagnostic; measure shipping code without counters. No new mutable optimization
feedback is allowed (**D8.4.1v2**).

**Exit:** every priority row has an output oracle, an identified hot operation,
dynamic hit/miss evidence where relevant, and an explicit evidence category.

### T15-1 — Make reserved constructor initialization a real fast path

First reproduce the diagnosis with an isolated compiler/runtime patch. Use a
root-cause repair in the ordinary property kernel, not a benchmark-name check
or source no-op in shipping tests.

1. Distinguish **absent reserved storage** from a live own property in the
   ordinary-add proof. Retain ordinary receiver, canonical name, extensibility,
   reserved-bit, flags/storage and prototype constraints.
2. Only after proving the original `[[Set]]` is an ordinary own-property
   creation, initialize through the existing shared storage/transition writer.
   Factor reusable mechanics from the literal initializer; keep Set and
   CreateDataProperty proofs distinct. Do not run a getter/setter or RHS twice.
3. Preserve source-order publication. A prototype setter may intercept a write
   and leave a reserved slot absent. Exceptions, non-extensibility, reflected
   receiver differences, deleted slots and descriptor changes use the complete
   kernel with precise roots and explicit completion.
4. Audit the shape after each real store and constructor exit. If dynamic
   type transitions invalidate the original exact-shape guard, do not keep
   emitting that guard as a presumed optimization. Choose a bounded sound
   plan: a statically proven final shape, a semantically valid fixed-slot
   representation/guard shared with the storage owner, or no specialization.
   Do not treat unrelated shapes as equal merely because offsets happen to
   match; no per-site learned shape lists.
5. Retain new admissions only where measured benefit pays for guard size,
   initialization and fallback. If the repair cannot make a case useful,
   narrow the effect/type-based admission with the diagnosed reason; preserve
   independent correctness work. Do not blindly revert all of Tune14.

**Required tests:** inherited getter/setter/non-writable property; setter
intercept with future own field absent; `preventExtensions`; descriptor
mutation; delete/re-add/order; mixed null/Number/string/object fields; primitive
and object constructor returns; RHS throw/reentry/receiver escape; direct eval;
class/plain constructors and aliases; retained objects under forced collection.
Extend existing `JsOpt` constructor fixtures and GC stress owners.

**Exit:** CD/Havlak/HashMap exact-predecessor release pairs recover the loss,
DeltaBlue/bounce/json/towers are independently checked, constructor traffic
shows fewer generic descriptors, and post-construction hits are real. Compare
against both R48 and C14. A semantic pass without this evidence is not done.

### T15-2 — Reduce common property work across the losing population

After T15-1, profile Havlak, Richards, HashMap, microdiff and JSON again; the
dominant cost may have changed. Target repeated work in the existing kernels:

- Preserve canonical `NameId` through lookup/store/descriptor decisions.
  Avoid re-interning or converting a key whose identity is already known.
- Reuse existing POD descriptor queries for internal ordinary-property
  decisions when their full semantics suffice; materialize a JS descriptor
  object only at an observable descriptor boundary or genuine fallback.
- Avoid repeated same-operation realm/brand/prototype classification after a
  guard has already established it. Do not retain such facts across callbacks,
  prototype mutations, coercions, proxies, or safepoints without a valid proof.
- Improve bounded compile-predicted literal/constructor field coverage only
  when the final representation is stable and dynamic misses are rare. Reuse
  `MirFieldAccessPlan`/shared emitter primitives; no second property planner.
- Attribute method/function dispatch separately before extending receiver-call
  shortcuts. The prior `mir_this` result does not justify more coverage alone.

**Exit:** a shared operation shows reductions in helper calls/allocation and
paired improvements on more than one canonical row, with miss-heavy controls.
For new *common physical* machinery, demonstrate an unannotated Lambda consumer
as well as JS, retire converged duplicate sequences, and retain profile-owned
semantics (**D1.3v3**, **D8.2.3–D8.2.6**).

### T15-3 — Complete bounded Number/array paths in search

The hot search workload is not chiefly repeated string decoding:
`text_search.js` constructs character-code arrays before the timer, then runs
naive/KMP/Boyer–Moore searches. Inspect `naiveSearch`, `kmpSearch`,
`prefixTable` and `boyerMooreSearch` first. Regex/UTF-16 builtin work cannot
explain those array-loop costs merely because the suite is named `text`.

For one measured loop at a time, keep index/length/loaded Number/comparison/
update/store in a useful native representation through its consumers:

1. Reuse the current Number and ordinary-array companion/index helpers; identify
   the missing producer or consumer, not just an unboxed function parameter.
2. Preserve holes, inherited numeric accessors, own indexed descriptors, bounds,
   signed zero, NaN, coercions, BigInt/Number separation and scalar ownership.
3. Hoist only invariant guards proven valid across the region. A callback,
   aliasing write, array growth or coercion can invalidate storage facts.
4. Reopen Tune14's general region framework only if a specific measured loop
   remains blocked by a shared proof limitation. Start with a bounded reuse of
   indexed effects and `MirValue`, not a new alias/range/liveness subsystem.

**Controls:** preserve oracle-valid Navier, FFT, quicksort, levenshtein,
binarytrees/gcbench and diviter. Recheck Mandelbrot/mbrot/puzzle in their own
isolated experiments; do not accept a regression because those rows still win
against QuickJS. Reject code growth without useful hit-path savings.

**Exit:** fewer boxed helpers on actual hot iterations, output equality plus
semantic boundary tests, release paired benefit and unchanged miss behavior.
Publish both the large sum benefit and the much smaller single-row geomean
contribution honestly.

### T15-4 — Broaden gains only from measured residuals

Next investigate revcomp/base64/fast_diff/log_pipeline/three_way_merge and
remaining AWFY losses. Classify string scanning, numeric indexing, property
enumeration, allocation or calls with profiles before choosing a patch.

Candidate mechanisms include bounded primitive string leaves, avoiding
redundant internal property-key/descriptor materialization, and cheaper array
element consumers. Keep UTF-16 behavior and method override/coercion effects.
Do not replace benchmark algorithms, cache benchmark answers, substitute a
special builtin for source loops, or weaken JS semantics to match MVP.

Measure frontend, MIR generation/native compilation, startup and peak memory
separately. The R48 cold-start gap is real, but it is not the cause of a
workload-timed 18-second CD run. Compiler/rooting work is conditional on
attributed costs; retain final cold-start reporting even if no patch is useful.

**Exit:** each selected residual has a measured change or an explicit
no-change/deferred disposition. No speculative catalog of specializations is
required to call the analysis complete.

### T15-5 — Cumulative acceptance, not a collection of micro-wins

| Gate | Required result |
|---|---|
| Beat QuickJS | `G(full JS/QuickJS) < 1.00` on all 63 rows in **two independent complete release sessions**. State that this is an aggregate, not every-row, claim. |
| Headroom/stretch | Aim for ≤0.90 engineering headroom; retain ≤0.80 as stretch. These are targets, not predicted package yields. |
| Absolute work | Reduce `T(candidate/C14)` below 1; publish `T(JS/QuickJS)`, suite ratios, worst losses and wins. Track `T(JS/QuickJS) <= 1` as a separate milestone, never infer it from G. |
| Regressions | Investigate every repeatable >3% workload/startup loss against the immediate predecessor and cumulative controls. Smaller systematic losses remain visible. No unresolved correctness regression. |
| Local acceptance | Normally ≥11 alternating pairs, extending to ≥31 for small/noisy effects, with raw samples and uncertainty. Diagnostic three-pair ablations are not this gate. |
| Dynamic benefit | Runtime hits/misses, helper/descriptor/allocation traffic and finalized hot MIR support the intended mechanism. Static admission counts alone fail. |
| Correctness | Focused JS boundaries, forced-GC/poison, optimizer/MIR contracts, `make test-lambda-baseline` and `make test262-baseline` on the final source. Retain Tune14's broader Test262 closeout as separately reported coverage. |
| Release identity | Rebuild with `make release` after any debug-building test target; archive/hash the actual measured executable and relevant modules/configuration. No debug performance data. |
| Reporting | Preserve all 63 rows, failures, timeouts and source/input hashes. Never shrink the population to passing/favorable rows. Report cold-start and memory separately. |

Useful partial work can be accepted without claiming the QuickJS milestone.
Conversely, a favorable matrix does not close structural/correctness gates.
MVP remains optional until a semantically matched release comparator exists.

## 6. Evidence, reproduction and remaining work

The analysis performed read-only source/history inspection, current manifest
verification, snapshot recalculation, exact archived release replay,
diagnostic source ablations and fresh release process sampling. It did not
rebuild or modify the production runtime, rerun the full 63-row suite, or claim
fresh runtime baseline results.
Result48 already supplies the full matrix; its recorded Test262 gate is
40,261/40,261 baseline cases with zero regressions.
The current R48 archive also passed a fresh three-engine frame-15/density
[Navier oracle](../../test/benchmark/js_tune15/r48_navier_oracle.json);
this separate correctness run does not alter the canonical timed workload.

Executable identities used or referenced:

| Archive | SHA-256 |
|---|---|
| R47 original `lambda-v47-fc0755a79d` | `80b287f337c86b41b530c2e03721768ba37096434db67c3a5e20588c908fc509` |
| C14 `lambda_t14_c14_release.exe` | `4b140f0c150d631033f896290c051e3536dbfa429d6341b26ed6f941def5d127` |
| R48 `lambda-v48-0e6f8abd89` | `c3c6560256874b6f258dab434f1933ddacdbeae2a5fc675f0524b74867b1931e` |

The R47 replay read its archive from the adjacent `Lambda-opus` workspace;
it did not rebuild an approximate R47 from current sources. Raw records retain
the actual absolute paths and hashes. Run one timing process at a time, on AC
power, without concurrent build/profile workloads.

```bash
python3 test/benchmark/js_tune15/analyze_results.py
python3 test/benchmark/verify_js_mvp_manifest.py \
  --manifest test/benchmark/js_tune14_manifest_v1.json
python3 test/benchmark/js_tune15/build_ablation_probes.py

python3 test/benchmark/run_paired_benchmarks.py \
  --control /Users/henryluo/Projects/Lambda-opus/test/benchmark/exe/lambda-v47-fc0755a79d \
  --candidate test/benchmark/exe/lambda-v48-0e6f8abd89 \
  --language js --suite awfy,jetstream --bench cd,hashmap \
  --pairs 11 --timeout 180 --output temp/tune15/r47_r48_cd_hashmap.json
```

For the class source ablation, copy the canonical `cd2_bundle.js` and
`havlak2_bundle.js` only into `temp/tune15/probes/`, then replace each match of
`constructor\s*\([^)]*\)\s*\{` with itself followed by `0;`. Leave the timer,
work count, class bodies and PASS checks otherwise intact. Use the existing
paired runner's `--manifest` source-pair facility, the same R48 executable on
both sides, three pairs and explicit `CD: PASS`/`Havlak: PASS` expected markers.
The [probe generator](../../test/benchmark/js_tune15/build_ablation_probes.py)
reproduces the exact diagnostic sources and writes the class, smaller-AWFY,
and HashMap source-pair manifests. Their source hashes were rechecked against
the raw paired artifacts. Comments only explain the no-op.

**Open implementation evidence:** isolated reserved-store repair; exact
post-transition guard-hit census; independently attributed remaining losses;
post-repair profiles; complete cumulative paired matrix; two QuickJS
confirmation sessions; final semantic/GC/runtime gates. These remain tasks,
not achievements of this proposal.

# JS Tune9 — Redesign Performance Recovery

**Date:** 2026-08-14

**Status:** ANALYSIS COMPLETE; P1, P2.1, and P2.2 recovery slices are
implemented with zero verified semantic regressions. Canonical post-tuning
benchmark work remains pending; Test262 batch stability is confirmed with zero
retry.

**Analysis anchor:** `0b27a30ea4485a851bf36aa291e009b0c65713ab`

**Measured snapshots:** [Result28](../../test/benchmark/Overall_Result28.md), [Result29](../../test/benchmark/Overall_Result29.md)

**Scope:** explain the Result28 → Result29 LambdaJS regression, assess Tune1–Tune8, and define the Tune9 recovery gates

## 1. Executive conclusion

Result29 is a real and major LambdaJS performance regression:

- the reported LambdaJS/Node geometric-mean ratio rose from **16.7×** in
  Result28 to **45.8×** in Result29, or **2.74× worse relative to Node**;
- across the 59 common benchmark rows, LambdaJS itself became **3.16× slower**
  by geometric mean;
- Node was also **1.15× slower** on the Result29 run, so host/run variance
  explains a minority of the ratio change, not the LambdaJS regression;
- Test262 still passed 40,261/40,261, but wall time rose from **101.10 s** to
  **141.60 s** (**+40.1%**).

The redesign's semantic decisions are not fundamentally wrong. The dominant
failure is in the performance architecture used to realize them:

1. Tune4's canonical constructed-prototype publication and Tune5's array
   companion storage interacted badly. A redundant canonical `Array.prototype`
   write created companion metadata, disabled dense `fill`/store paths, and
   pushed ordinary numeric loops through full semantic property operations.
2. Tune5's indexed lane did not recognize the common MIR representation of an
   integral JavaScript Number: a native `double`. Loop indices therefore paid
   `ToPropertyKey`, lane classification, and generic `[[Set]]` work repeatedly.
3. The compiler routed computed reads and writes through the semantic kernel
   before the redesign's feedback-vector phase existed. The semantic authority
   was correct, but there was no guarded direct path around it for proven dense
   arrays and typed arrays.
4. Tune5 and Tune6 declared non-regression performance gates, then closed
   implementation without release A/B evidence. Mechanism/LOC completion was
   allowed to outrun measured hot-path acceptance.

The local post-Result29 tuning run at `bf8cb8b6633…` confirms this diagnosis. On
the 57 rows with numeric values in all three datasets and an `ok` tuned-run
status, the LambdaJS/Node ratio changed from **15.68×**
in Result28 to **43.85×** in Result29, then recovered to **23.71×** after the
current dense-index and prototype-publication fixes. That is a **1.85× recovery
from Result29**, but the tuned tree remains **1.51× slower than Result28** on
those rows. Two tuned rows timed out, so this is diagnostic evidence, not a
publishable Result30.

Tune9 therefore has two obligations: retain the redesign's observable
semantics, and recover the old design's hot-path efficiency through exact
guards, direct operations, hoisted facts, and a single authoritative fallback.

## 2. Authority and invariants

This recovery must preserve the formal design rather than restoring old
semantic shortcuts:

- **D3.4.4v2**: shape property identity is one `NameId` field.
- **D3.4.7**: JavaScript `TypeMap` metadata may carry immutable class/prototype
  family metadata and one immutable exotic-operations table.
- **D4.6.1v2–D4.6.2v2**: one semantic property identity is `NameId`, built by
  evolving the existing `NamePool`.
- **D5.1–D5.4**: fast paths do not weaken precise rooting, scalar-home, cleanup,
  or context-address invariants.
- **D6.2.2v2**: calls and construction preserve per-callee executable dispatch,
  observable property access, and `newTarget`/prototype semantics.
- **D8.4.3**: fallible JS/Jube helpers use the merged in-band Item error ABI.

The formal specifications do not currently define ECMAScript array-index
classification in enough detail to replace the ECMAScript rules. Tune9 must
therefore preserve the existing JS semantic kernel for every unproved key,
receiver, descriptor, prototype, Proxy, and exotic case. A fast path is valid
only when its guard proves that its result is identical to that kernel.

The central design rule is:

> One semantic authority does not require one physical execution path.

The correct shape is a proof-carrying guard followed by a direct operation,
with a miss falling back to the single semantic implementation. Tune9 must not
create a second, drifting implementation of JavaScript property semantics.

## 3. Measurement scope and limits

### 3.1 Snapshot provenance

| Snapshot | Date | Commit | Rows | Test262 | LambdaJS/Node |
|---|---:|---|---:|---:|---:|
| Result28 | 2026-08-10 | `e91432d4aa…` | 59 | 40,261/40,261 in 101.10 s | 16.7× |
| Result29 | 2026-08-13 | `211fea19…` | 59 | 40,261/40,261 in 141.60 s | 45.8× |

The published Result28 aggregate includes `larceny/triangl`, whose LambdaJS
status is `partial_ok` but has a reported timing. The 59-row absolute comparison
in this document follows the published timed values. Excluding that row gives
58 strict `ok`/`ok` common rows: LambdaJS is still 3.14× slower and Node 1.15×
slower in Result29, so the conclusion does not depend on the partial row.

Result28 was recorded after Tune1 and Tune2 but before Tune3. Result29 was
recorded after the Tune3–Tune8 landing sequence. The measured delta is therefore
the combined effect of Tune3–Tune8 and intervening commits, not Tune1 or Tune2.
It is not valid to assign exact percentages to individual tuning phases from
these two snapshots alone.

### 3.2 Causal confidence

Result28 and Result29 are sequential historical runs, not an interleaved
archived-binary A/B experiment. Machine state and the Node denominator changed:
Node's common-row geometric mean was 1.15× slower in Result29. Absolute phase
attribution therefore still requires interleaved runs of archived Result28,
archived Result29, and the current release binary.

That limitation does not make the primary diagnosis ambiguous. The **6,142×**
Sieve regression, the Result29 profile/trace counters, the exact companion-map
construction path, and the multi-thousand-fold recovery after preventing that
path form a consistent causal chain. The uncertainty is in how the remaining
regression is divided among Tune3–Tune8, not in the dominant array failure.

## 4. Result28 versus Result29

### 4.1 Suite-level ratios

Lower is better. Ratios are LambdaJS time divided by Node time.

| Suite | Result28 | Result29 | Ratio change |
|---|---:|---:|---:|
| R7RS | 5.81× | 8.06× | 1.39× worse |
| AWFY | 25.9× | 184× | 7.10× worse |
| BENG | 7.49× | 12.9× | 1.72× worse |
| KOSTYA | 15.9× | 54.4× | 3.42× worse |
| LARCENY | 14.7× | 48.9× | 3.33× worse |
| JetStream | 71.2× | 95.4× | 1.34× worse |
| Text | 62.8× | 80.6× | 1.28× worse |
| **Overall** | **16.7×** | **45.8×** | **2.74× worse** |

AWFY, KOSTYA, and LARCENY are the clearest signature: tight numeric loops over
ordinary arrays and typed arrays were disproportionately damaged. The broad
JetStream/Text movement and Test262 wall-time increase show that the problem is
not limited to one benchmark fixture.

### 4.2 Largest absolute LambdaJS regressions

Times are milliseconds from the checked-in JSON snapshots.

| Benchmark | Result28 | Result29 | Result29 / Result28 |
|---|---:|---:|---:|
| `awfy/sieve` | 0.477 | 2,930.852 | **6,141.67×** |
| `larceny/puzzle` | 25.654 | 6,457.469 | **251.71×** |
| `awfy/permute` | 9.880 | 625.496 | **63.31×** |
| `kostya/primes` | 101.785 | 4,607.991 | **45.27×** |
| `awfy/queens` | 6.447 | 242.043 | **37.54×** |
| `awfy/nbody` | 582.944 | 5,699.268 | **9.78×** |
| `larceny/quicksort` | 64.818 | 489.511 | **7.55×** |
| `kostya/levenshtein` | 80.880 | 587.942 | **7.27×** |
| `jetstream/cube3d` | 659.316 | 4,602.149 | **6.98×** |
| `text/microdiff` | 1,490.865 | 9,883.083 | **6.63×** |
| `beng/revcomp` | 48.831 | 307.179 | **6.29×** |
| `awfy/bounce` | 4.996 | 29.327 | **5.87×** |

This distribution rules out a simple fixed startup penalty. The largest losses
scale with repeated element/property operations inside loops.

## 5. Root-cause analysis

### 5.1 Redundant canonical prototype publication poisoned dense arrays

Tune4 correctly centralized `GetPrototypeFromConstructor` behavior for
construction. In Result29, the publication path applied the resolved prototype
even when an array constructor had already installed the same canonical
`Array.prototype`.

For arrays, `js_set_prototype` requires companion metadata. The redundant write
therefore created a companion map on an otherwise dense ordinary array. Tune5's
dense `fill` and element-store guards treated the existence of companion data as
proof that the array needed generic property semantics. As a result:

1. `new Array(n)` created a dense array;
2. constructor finalization redundantly published the canonical prototype;
3. publication allocated array companion metadata;
4. `.fill(value)` rejected the dense path and performed per-index semantic
   stores;
5. subsequent loop writes continued through companion indexed slots and the
   generic property kernel.

This is why a semantically harmless redundant prototype write had catastrophic
performance consequences.

The current tree addresses the construction half in
`js_apply_resolved_constructed_prototype()` (`lambda/js/js_runtime.cpp`): for
arrays, it compares the installed prototype with the resolved prototype and
does not publish an identical canonical prototype again. The code comment at
the fix point records the protected invariant: an identical write must not
create a companion map and disable dense array paths.

### 5.2 Integral Number keys missed the indexed lane

Tune5's architecture distinguishes property keys by `NameId` or unsigned
32-bit array index. That is the right semantic split, but Result29 selected the
indexed path primarily from inferred integer representation. JavaScript loop
counters are commonly carried by MIR as native doubles even when their values
are exact integers.

An expression such as `a[i] = value` therefore took this route on each
iteration:

1. box/interpret the Number as a property key;
2. run `ToPropertyKey`;
3. classify the resulting key into a property lane;
4. call the receiver-aware semantic `js_set` operation;
5. convert the operation result into assignment completion.

The current `js_number_key_to_index_fast()` helper fixes the representation
gap with exact guards: finite, non-negative, integral, within the supported
index range, and round-tripping without loss. Fractional values, `NaN`,
infinities, negative values, and out-of-range values miss the fast path and
retain ordinary property-key semantics.

### 5.3 Semantic plumbing dominated actual storage work

The Result29 profiles make the imbalance concrete.

For `awfy/sieve`:

| Counter | Calls / time |
|---|---:|
| `js_to_property_key` | 11,075 calls |
| `js_property_lane_for_canonical_key` | 11,069 calls |
| `js_set` | 11,069 calls |
| `js_assignment_set_result` | 11,069 calls |
| top-level array property branch | 16,069 calls / 1,352.835 ms |
| `array_numeric_companion_data` | 11,069 calls / 1,163.374 ms |
| regular numeric array store | 5,000 calls / 122.650 ms |
| value-to-number conversion | 5,000 calls / 122.294 ms |

The trace reported **0 dense fast-store hits** and **11,069 guard-fail
fallbacks**.

For `kostya/primes`:

- `js_to_property_key`, lane classification, `js_set`, and assignment
  completion each ran **2,122,050** times;
- module-name and TDZ plumbing also ran about **2.12 million** times;
- name lookup ran **6,334,408** times, with **6,330,515 misses**;
- the actual typed-array numeric branch consumed only **124.306 ms** total,
  about **58 ns per call**.

The storage operation was not intrinsically slow. Repeated classification,
lookup, metadata, and completion plumbing around it dominated execution.

### 5.4 The optimization phase came after the semantic replacement

The redesign roadmap makes JR6 the unified semantic property kernel and JR8
the feedback-vector/IC mechanism. Its phase table lands R4/JR6 before R7/JR8.
Tune8 is module-foundation work, not the JR8 feedback-vector implementation.

That order left a dangerous interval: old specialized property paths were
removed or bypassed, the new semantic path was already mandatory, and the new
guarded cache/direct-call layer did not yet exist. Correctness remained high,
but every loop iteration paid cold-path semantic costs.

JR8 should not be treated as an optional later speedup after deleting existing
hot paths. Either guarded replacements must land in the same phase as the
semantic migration, or JR8-equivalent feedback must be a prerequisite for
removing those paths.

### 5.5 Performance acceptance was specified but not enforced

Tune5 and Tune6 both state that release property/object/array benchmarks must
show no material regression. Tune6 also explicitly says that “JR8 will fix it”
is not an acceptance waiver. Their completion records nevertheless state that
release A/B evidence was not asserted or not requested.

That is a process defect in the redesign:

- structural convergence, helper counts, and LOC reduction were treated as
  closeout evidence;
- the non-negotiable release-performance gate was not run;
- the first full-suite measurement after several phases had too broad a change
  interval for clean attribution.

Future Tune phases must not be marked done when an explicitly required
performance gate is unchecked. A missing baseline means the phase remains
performance-unverified, not accepted.

## 6. Tune1–Tune8 assessment

| Tune | Design assessment | Performance assessment |
|---|---|---|
| **Tune1 Runtime** | The D8.4.3 in-band error ABI is sound and removes split pending-exception state. | Already present in Result28; not part of the Result28→29 regression. |
| **Tune2 Exception** | Correctly publishes raw scalar/error results and cleans up the helper catalog. | Already present in Result28; not part of this delta. |
| **Tune3 Name** | D3.4.4v2 and D4.6.1v2–D4.6.2v2 give property identity one coherent `NameId` representation. | Broad name-table loads and miss-heavy lookups are visible in `primes`; the phase needs isolated release A/B and hoisting/caching work. |
| **Tune4 Callable** | D6.2.2v2 correctly preserves observable `Get`, call/construct distinction, `newTarget`, and constructor prototype behavior. | Its own A/B already showed large costs for static source methods and Date construction. The redundant canonical array-prototype publication became the trigger for Tune5's catastrophic dense-array fallback. |
| **Tune5 Property** | One receiver-aware semantic kernel plus explicit indexed/named lanes is the right authority model. | This is the primary regression source: integral-double keys missed the indexed lane, companion presence was an overly coarse guard, and generic computed access paid the full kernel in loops. Its release non-regression gate was not satisfied. |
| **Tune6 Object** | D3.4.7 immutable class metadata and one exotic ops table are a coherent replacement for scattered class tests. | Ordinary operations still pay metadata/null-ops checks and no release A/B was recorded. This is likely a secondary broad cost and needs measurement. |
| **Tune7 Promise** | The GC-owned Promise/job direction and precise ownership are consistent with D5 and the redesign. | Still in progress and unlikely to explain numeric-loop catastrophes; it may affect async/Test262 wall time and needs a separate release gate. |
| **Tune8 Module** | The current work is module-foundation migration, not JR8 feedback vectors. | Also in progress; not a plausible cause of the Sieve failure, but repeated module-name/TDZ work appears in `primes` and may contribute to broad execution/startup cost. |

### 6.1 Known Tune4 correctness costs

Tune4's focused release A/B recorded the following changes:

Negative values are improvements; positive values are regressions.

| Case | Change |
|---|---:|
| ordinary dynamic call | -5.60% |
| intrinsic call | -31.95% |
| static source method | **+220.23%** |
| computed source method | -5.47% |
| dynamic Date construction | **+31.46%** |
| bound call | -11.95% |
| bound construction | -1.56% |
| startup | +0.71% |

The static-method and Date costs arise from restoring observable property and
constructor behavior that old shortcuts could skip. Tune9 must optimize these
cases with stable-callee/prototype guards and fallback, not restore an
incorrect shortcut. The table also shows why the redesign needed performance
work before proceeding to additional broad migrations.

## 7. What is wrong in the redesign—and what is not

### 7.1 Sound decisions to retain

- one `NameId` property identity;
- one authoritative semantic property kernel;
- receiver-aware operations that preserve descriptors, prototypes, accessors,
  Proxies, strictness, and exotics;
- immutable class metadata and a single exotic-ops table;
- correct `Call`/`Construct` and `newTarget` semantics;
- the merged Item error ABI;
- precise rooting and context-local runtime state.

### 7.2 Design and execution errors to correct

1. **Semantic unification was confused with physical-path unification.** A
   dense store does not need to execute every generic semantic layer when guards
   already prove the answer.
2. **Companion state is too coarse.** `arr->extra != 0` conflates custom
   prototype state, named own properties, numeric descriptor overlays, sparse
   elements, extensibility, and other metadata. Fast-path eligibility needs the
   exact disqualifying facts, not a blanket companion test.
3. **Index classification depended on representation.** It must recognize every
   valid integral JavaScript Number representation while preserving fallback
   behavior for non-indices.
4. **Loop-invariant facts were recomputed.** Name IDs, module slots, TDZ state,
   elements kind, class metadata, typed-array element type, prototype-index
   epoch, and stable callee targets should be hoisted or cached when valid.
5. **Replacement optimization arrived too late.** JR8/feedback-vector work must
   move earlier or ship with each hot-path semantic migration.
6. **Performance gates were waivable in practice.** A Tune phase must remain
   open until release A/B and the relevant suite gate pass.
7. **Structural metrics were overweighted.** Helper/LOC consolidation is useful
   only when correctness and performance remain within their budgets.

## 8. Current recovery evidence

The current tree contains five relevant recovery mechanisms:

1. `js_apply_resolved_constructed_prototype()` avoids an identical canonical
   prototype publication that would create array companion metadata.
2. `js_number_key_to_index_fast()` recognizes exact integral Number keys.
3. `jm_transpile_index_assignment_kernel()` emits specialized assignment paths
   before falling back to the semantic operation.
4. `js_elements_set_existing_dense_int_fast()` performs a guarded existing
   dense-element store while preserving generic fallback.
5. Named-property feedback-vector lowering is selected by
   `LAMBDA_INLINE_CACHE` in release as well as profile configurations;
   `LAMBDA_JS_EXEC_PROFILE` now controls only observability, not whether
   `js_get_name_id_ic()` / `js_set_name_id_ic()` are emitted.

The local `temp/result29_tuned_clean_release.json` run, captured from commit
`bf8cb8b6633f853d0a971eed3ae5fdd160086656`, produced this same-row comparison:

| Dataset | Common rows | LambdaJS/Node geomean |
|---|---:|---:|
| Result28 | 57 | 15.68× |
| Result29 | 57 | 43.85× |
| post-Result29 tuned release | 57 | 23.71× |

Selected recoveries:

| Benchmark | Result29 | Tuned | Speedup |
|---|---:|---:|---:|
| `awfy/sieve` | 2,930.852 ms | 1.157 ms | **2,533×** |
| `larceny/puzzle` | 6,457.469 ms | 69.052 ms | **93.5×** |
| `kostya/primes` | 4,607.991 ms | 140.323 ms | **32.8×** |
| `awfy/queens` | 242.043 ms | 6.865 ms | **35.3×** |
| `awfy/permute` | 625.496 ms | 27.237 ms | **23.0×** |

This recovery strongly validates the primary diagnosis. It is incomplete:

- `navier_stokes` and `hashmap` timed out in the tuned run;
- the 57-row tuned ratio is still 1.51× worse than Result28;
- `nbody`, `cd`, `havlak`, `bounce`, and `revcomp` showed little recovery or
  regressed, indicating residual broad costs or different bottlenecks;
- no clean, interleaved archived-binary A/B has yet controlled host variance;
- no complete 59-row post-tuning result has been published.

The tuned JSON is therefore a diagnostic artifact. It must not replace
Result29 or be labeled Result30.

### 8.1 P2.1 release named-IC recovery (2026-08-14)

P3 profiling of `awfy/nbody` identified a release-only compiler gate rather
than a new property-semantics defect. The runtime already has guarded named
load/store IC implementations. However, `js_mir_expression_lowering.cpp` also
required `JS_EXEC_PROFILE_ENABLED` before emitting them. Consequently,
`LAMBDA_INLINE_CACHE=1` was present in a normal release build but named member
operations still bypassed those ICs and entered the generic property kernel.

The fix preserves the D3.4.4v2/D4.6.1v2 `NameId` authority and the D8.4.3
fallback ABI: only the existing `LAMBDA_INLINE_CACHE` guard now controls IC
emission. A cache miss still calls the same semantic operation; profile builds
only add counters. This is not a new semantic property implementation.

Diagnostic evidence (not an acceptance measurement):

| Configuration | `awfy/nbody` result | Interpretation |
|---|---:|---|
| Profile ICs enabled | 2,960.769 ms | Existing feedback path is effective. |
| Profile ICs disabled | 8,486.388 ms | Same profile binary; validates the IC contribution. |
| Release before the lowering fix | 7.38 s | One-sample residual probe; not interleaved. |
| Release after the lowering fix | 2,364.617 / 2,374.176 / 2,363.807 ms | Three correct runs; median 2,364.617 ms, about 3.1x faster than the pre-fix probe. |
| Final tree after the live-DOM IC boundary | 3,180.387 / 3,200.743 / 3,019.025 ms | Three correct release runs; median 3,180.387 ms, still about 2.3x faster than the pre-fix probe. |

The two release sample groups were not interleaved and the later group followed
the full baseline runs, so their difference is environmental noise until P0
captures the controlled A/B bundle. They establish that the final guarded tree
still executes the optimized path, not a publishable benchmark delta.

The profile recorded 9,648,206 named-load probes and 2,700,039 named-store
probes for this workload. The focused JS optimization contract suite passed all
19 tests after the change, including named IC warming and the Tune9 indexed
semantic guards. Enabling release ICs initially exposed a stale-read regression
in live DOM form collections: their named entries are derived from the current
tree and must run the existing refresh hook. The IC receiver admission now
rejects only arrays registered as live DOM collections, so they use the single
semantic fallback while ordinary array companion properties remain cacheable.

`make test-lambda-baseline` passed **3,718/3,718** on this tree, including all
21 JS MIR-emission fixtures and all 347 JS file tests. Two pinned Test262
baseline runs completed with zero pass-to-fail regressions across all 40,261
baseline entries. Both runs killed the same seven tests from four worker batches,
but every affected test passed in its isolated retry. The repeatable batch-only
failure was not a masked runtime result; the runner-level cause and its final
zero-retry verification are recorded in P2.2 below.

### 8.2 P2.2 Test262 intrinsic-function batch reset (2026-08-14)

The seven repeatable batch-only recoveries were a hot-realm isolation defect,
not a worker timeout or retry-policy failure. The reset snapshot restored each
constructor and prototype Map, but a method value such as
`String.prototype.split` is a separate function object with its own
`properties_map`. A valid preceding Test262 source deletes the configurable
`split.length` property. The prototype still pointed to the same function after
reset, while that function's descriptor map remained mutated, so the following
metadata test inherited the deletion. The same missing boundary affected
`Array.prototype.forEach` metadata and other intrinsic method functions.

`js_globals.cpp` now captures every function (including accessor getter/setter
functions) reachable from the intrinsic constructor/prototype snapshot maps.
It preserves and restores each function's own prototype and property maps,
roots each pristine shadow across GC, and treats those maps as snapshot-backed
when a descriptor mutation needs a private TypeMap. This extends the existing
D6.2.2v2/D6.2.4 callable-object and precise-rooting boundaries without
special-casing the recovered test names or changing Test262 runner
classification.

Focused `js-test-batch` probes first deleted then verified restoration of
`String.prototype.split.length`, `Array.prototype.forEach.length`, and
`Array.prototype.forEach.name`; all second sources passed after the reset. The
authoritative release gate, with `ref/test262` pinned at
`673e9bacbe28590f501e2dcd817aadcc31899191`, then reported:

| Gate | Result |
|---|---|
| `make test-lambda-baseline` | 3,718 / 3,718 passed |
| `make test262-baseline` | 40,261 / 40,261 fully passed; 0 non-fully-passing; 0 failed; 0 retry; 0 regressions; 211.0 s |

## 9. Tune9 performance architecture

### 9.1 Guarded direct operation with one fallback

Every optimized property/call site should have this shape:

```text
prove receiver/key/shape/prototype/descriptor facts
    -> direct dense, typed, named, or stable-call operation
    -> preserve result/error/rooting ABI
otherwise
    -> call the single semantic kernel
```

The guard is part of correctness, not merely an optimization hint. It must be
invalidated or missed when any observable fact changes.

### 9.2 Split array companion facts

Replace blanket “has companion” exclusion with explicit facts sufficient for
the operation, for example:

- custom prototype installed;
- own named properties present;
- indexed descriptor/accessor overlay present;
- sparse or dictionary elements present;
- non-extensible/sealed/frozen state;
- prototype-index epoch is not clean;
- exotic/Proxy receiver or receiver mismatch.

The exact representation may be flags, elements kind, epochs, or immutable
shape metadata. It must avoid duplicate semantic state and must preserve D3.4.7
metadata invariants.

### 9.3 Recognize all numeric-key representations

The indexed classifier must accept exact valid indices carried as:

- inferred native integers;
- exact integral native doubles;
- boxed Number values where the optimized lowering can prove the same facts.

Numeric `-0` must behave exactly like the generic `ToPropertyKey` path and
address index zero; the string key `"-0"` remains a distinct semantic boundary.
Negative non-zero/fractional numbers, `NaN`, infinities, out-of-range integers,
Symbols, and strings that are not canonical array indices must reject the
indexed fast path. Tests, not assumptions, define each boundary.

### 9.4 Hoist and cache stable facts

Move repeated work out of loops or into feedback slots when validity can be
proved:

- canonical `NameId` constants;
- module binding slots and immutable active-module identity;
- TDZ checks after dominating initialization when control flow proves safety;
- elements kind and typed-array element type;
- shape/class metadata and null exotic-ops state;
- prototype-index epoch;
- stable source/intrinsic callee target and constructor prototype.

This is the intended role of JR8-style feedback. A cache miss must remain
observationally invisible and enter the same semantic operation used today.

## 10. Execution plan

### P0 — Establish a canonical A/B harness

- Build or restore release binaries for Result28, Result29, and current Tune9.
- Run identical workload files and engine versions on AC power with no
  concurrent builds.
- Interleave binaries per benchmark row rather than completing one whole
  snapshot before the next.
- Collect at least five measured samples after warm-up and preserve raw samples,
  median, geometric means, timeouts, and environment metadata.
- Capture the same profile counters for representative dense array, typed array,
  named property, call, module, and startup workloads.

**Exit:** the Result28→Result29 regression and current recovery reproduce under
controlled conditions, or this diagnosis is revised before more tuning.

### P1 — Finish indexed read/write recovery

- Validate the current integral-double and dense existing-store fast paths.
- Add equivalent guarded reads and legal append/growth paths where profiles
  justify them.
- Replace coarse companion rejection with exact eligibility facts.
- Cover ordinary arrays, packed/holey/tagged transitions, typed arrays, detached
  buffers, descriptors, prototype accessors, Proxies, receiver mismatch,
  strict/sloppy stores, `super[index]`, and GC/scalar-home boundaries.
- Profile `sieve`, `puzzle`, `permute`, `primes`, `queens`, `nbody`, and
  `levenshtein` after each material change.

**Exit:** catastrophic numeric-loop regressions are gone with zero semantic
fixture regression and no timeout.

### P2 — Recover named property and call sites

- Measure named IC hit/miss rates and eliminate repeated misses that can be
  guarded by shape/prototype epochs.
- **Completed P2.1:** emit the existing guarded named load/store feedback slots
  in normal release builds; the profile-only macro must not suppress them, and
  exclude live DOM collections whose properties require a refresh hook.
- Hoist constant `NameId` and module slot loads.
- Add stable-callee direct call/construct feedback while preserving observable
  `Get`, receiver, bound function, `newTarget`, and prototype behavior under
  D6.2.2v2.
- Re-run the Tune4 static-method and Date-construction microbenchmarks.

**Exit:** known Tune4 correctness costs have guarded optimized paths, and misses
still execute the single callable/property authority.

### P3 — Isolate residual broad costs

- Profile rows that did not recover: `nbody`, `cd`, `havlak`, `bounce`, and
  `revcomp`.
- Separate parse/compile, module initialization, startup, and steady-state
  execution time.
- Attribute Test262's +40.1% wall-time increase between per-test startup,
  module/name/TDZ plumbing, property operations, and GC.
- Do not infer a common cause where profiles show different bottlenecks.

**Exit:** every remaining >1.25× Result28 regression has a measured owner and a
tracked fix or an explicitly approved semantic-cost exception.

### P4 — Publish the canonical result

- Run all correctness and release-performance gates below.
- Produce a complete 59-row benchmark result with zero missing cells/timeouts.
- Record the exact commit, archive, tool versions, environment, raw JSON, and
  comparison against both Result28 and Result29.
- Publish as the next numbered result only after acceptance; do not overwrite
  historical snapshots.

## 11. Required optimization tests

Focused gtests and MIR/runtime fixtures must cover the guards introduced in this
session, including:

1. construction with the canonical array prototype does not allocate companion
   metadata or disable dense `fill`/stores;
2. a genuinely custom constructor prototype is still published and observed;
3. integral double indices take the indexed path for ordinary and typed arrays;
4. numeric `-0` addresses index zero, while string `"-0"`, fractional,
   negative non-zero, `NaN`, infinite, and boundary/out-of-range keys fall back
   with correct property semantics;
5. dense existing stores preserve elements kind, length, assignment completion,
   and in-band exception behavior;
6. holes, growth, descriptors, accessors, non-extensibility, custom prototypes,
   prototype-index pollution, Proxies, receiver mismatch, and `super[index]`
   miss the fast path when required;
7. typed-array detached/out-of-bounds/conversion behavior remains correct;
8. fast and fallback paths preserve D5 rooting/scalar homes across `MAY_GC` and
   D8.4.3 error propagation;
9. profile counters or test-only observability prove both a fast-path hit and a
   guarded fallback, rather than testing only the final value.

Tests should assert semantic and structural invariants. They must not hard-code
benchmark inputs or special-case benchmark names.

## 12. Acceptance gates

### 12.1 Correctness

- focused optimization gtests pass;
- `make test-lambda-baseline` passes;
- `make test262-baseline` passes with no failure, crash, timeout, or retry
  introduced by Tune9;
- GC/rooting stress coverage for every new path passes.

### 12.2 Performance

All measurements use release builds.

- complete all 59 canonical rows with zero timeout/missing cell;
- primary gate: common-row LambdaJS/Node geometric mean is no worse than
  **Result28 +5%** under the interleaved harness;
- stretch gate: restore or beat Result28's published **16.7×** overall ratio;
- no benchmark is more than **1.25× slower** than its interleaved Result28
  baseline without a measured, documented, approved semantic-cost exception;
- zero unexplained **>2×** regression;
- Test262 wall time is no worse than **Result28 +10%** under the controlled
  harness;
- the Tune4 static-method and Date-construction cases have explicit budgets and
  profiles rather than being hidden by the suite geometric mean.

Historical 16.7× and 101.10 s values are reference targets, not substitutes for
same-host interleaved baselines. If the archived Result28 binary does not
reproduce those values, acceptance uses its controlled rerun and records the
difference.

### 12.3 Design and structure

- one semantic property implementation remains authoritative;
- direct paths are guarded and fall back rather than duplicating semantics;
- no conservative stack scanning or weakened precise-rooting rule;
- no benchmark-specific branch or hard-coded workaround;
- every completed Tune phase includes its release A/B evidence;
- mechanism/LOC targets cannot override failed correctness or performance gates.

## 13. Stop and rollback rules

- If a direct path cannot prove semantic equivalence, narrow its guard and use
  fallback; do not restore an incorrect old shortcut.
- If a cache or feedback slot lacks a complete invalidation condition, keep the
  site uncached.
- If a tuning change improves the aggregate while causing an unexplained >2×
  row regression, stop and diagnose before continuing.
- If full-suite correctness fails while focused tests pass, the phase remains
  unverified; do not mask Test262 in its gtest runner.
- Preserve Result28 and Result29 as immutable evidence. A partial or timed-out
  local run is never promoted to a numbered result.

## 14. Completion checklist

- [x] Result28 and Result29 snapshot comparison recorded.
- [x] Common-row absolute LambdaJS and Node movement separated.
- [x] Dominant array companion/index-lane root cause identified.
- [x] Tune1–Tune8 design and performance assessment recorded.
- [x] Current partial recovery measured and labeled diagnostic.
- [ ] Controlled archived Result28/Result29/current interleaved A/B completed.
- [ ] Indexed access recovery complete across all semantic boundaries.
- [x] P2.1 release named load/store IC lowering is enabled and has a measured
  `nbody` diagnostic recovery.
- [ ] Named property, module, and callable residuals profiled and tuned.
- [x] Focused optimization gtests and MIR/DOM regression fixtures complete.
- [x] Lambda baseline passes on the final tree (3,718/3,718).
- [x] Test262 baseline has zero pass-to-fail regression (40,261 entries).
- [x] Test262 batch stability is confirmed with zero retry (P2.2).
- [ ] Full 59-row release benchmark completes with no timeout.
- [ ] Tune9 acceptance targets pass and the next numbered result is published.

## 15. Evidence inventory

Checked-in evidence:

- `test/benchmark/Overall_Result28.md`
- `test/benchmark/benchmark_results_v28.json`
- `test/benchmark/Overall_Result29.md`
- `test/benchmark/benchmark_results_v29.json`
- `vibe/jube/JS_Runtime_Redesign.md`
- `vibe/jube/JS_Tune1_Runtime (done).md`
- `vibe/jube/JS_Tune2_Exception (done).md`
- `vibe/jube/JS_Tune3_Name (done).md`
- `vibe/jube/JS_Tune4_Callable (done).md`
- `vibe/jube/JS_Tune5_Property (done).md`
- `vibe/jube/JS_Tune6_Object (done).md`
- `vibe/jube/JS_Tune7_Promise.md`
- `vibe/jube/JS_Tune8_Module.md`
- `doc/Lambda_Formal_Design.md`
- `doc/Lambda_Formal_Semantics.md`

Transient diagnostic evidence currently under `temp/`:

- `temp/result29_sieve.profile`
- `temp/result29_sieve.trace`
- `temp/result29_primes.profile`
- `temp/result29_tuned_clean_release.json`
- `temp/tune9_residual_probe.json`
- `temp/tune9_nbody_profile.tsv`
- `temp/tune9_lambda_baseline.log`
- `temp/tune9_test262_baseline.log`
- `temp/tune9_lambda_baseline_final.log`
- `temp/tune9_test262_baseline_final.log`
- `temp/_t262_batch_kills.txt`
- `temp/tune9_function_metadata_reset_after.log`
- `temp/tune9_intrinsic_method_reset_after.log`
- `temp/tune9_batch_reset_lambda_baseline.log`
- `temp/tune9_batch_reset_test262_baseline.log`

The transient files support this analysis but are not a reproducible benchmark
archive. P0 must replace them with a complete capture bundle before Tune9 is
closed.

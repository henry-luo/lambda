# JS MVP: core language and library completion

**Date:** 2026-10-13. **Status:** features and conformance milestone complete; 21,747 audited Test262 passes. Performance gaps remain open.
**Design:** [JS_MVP_Lmd §33](../jube/JS_MVP_Lmd.md#33-core-language-and-library-completion-5000-audited-test262-passes).

## 1. Goal and measurement

Reach 5,000 distinct fully passing IDs in the frozen 40,261-ID LJS baseline,
using the unchanged canonical harness, required variants, negative phases and
async completion policy. Use one pinned MVP MIR executable, record source and
binary hashes, and replay missing/crash/timeout outcomes individually without
discarding the original evidence. Admission/engine failures never count as a
successful negative test. Do not add subset counts from different builds.

First capture the current whole-target inventory. The previous 8,888 count is
diagnostic; the last 1,913/1,949 result only covers selected patterns. Record the
entry selected-mode count separately; the milestone requires the candidate
audit below even if entry already has more than 5,000 selected-mode passes.
No baseline/harness change may hide failures (**S1.11**, **D1.3v3**).

## 2. Work and order

| Work | Implementation boundary | Acceptance evidence |
|---|---|---|
| Object/intrinsics | Complete descriptor conversion/validation, defineProperty/defineProperties, own-key order, integrity operations and native callable metadata | Original Object/Reflect/Function cases, descriptor effect order, primitive receivers, proxy abrupt paths |
| Arrays | Complete generic receiver, length/index, inherited/holey reads, callback this/mutation and abrupt behavior in existing operations | Canonical Array families plus dense/sparse, inherited and reentrant focused cases |
| Public classes | Expressions and local declarations, lexical names/captures, computed public members, static blocks and initialization/constructor/super order | Class families and newly unblocked pattern/arguments cases; private/async and builtin subclass dependencies remain explicit |
| Strings/primitives | Complete ordinary operations, wrapper conversions, UTF-16 indexing and String.raw | Canonical String/Number/Boolean families and conversion-order/Unicode cases |
| Bindings/templates | Global declarative versus object bindings; stable frozen cooked/raw template arrays and substitution order | Global binding conflicts/descriptors and tagged template identity, receiver, invalid escape and abrupt tests |

Prioritize reproduced shared root causes from the fresh inventory, not isolated
test-name accommodations. Global/class environment prerequisites precede their
consumers. Existing methods are extended rather than reimplemented wholesale.
Private classes, interpreter interoperability, async/modules and advanced buffers
remain later work; eval/Function bodies retain **D8.1.3v24**'s interpreter policy.

## 3. Reuse and performance contracts

- Reuse Lambda nominal Maps/shapes, canonical keys, Array storage, destination
  scalar homes and precise RootFrame/Rooted ownership (**D2.6.9v3**, **D2.7.1**,
  **D3.4.3v5**, **D5.2–D5.3**). Ordinary objects do not become VMaps.
- Start descriptor work in `mvp_lmd_objects.cpp`, `mvp_lmd_reflect.cpp` and the
  existing `js_descriptor_from_object_with_reader`/descriptor validation kernel.
- Reuse sequence length/index, iterator, property and callable operations from
  `mvp_lmd_arrays.cpp`, `mvp_lmd_functions.cpp`, `mvp_lmd_library.cpp` and
  `mvp_lmd_strings.cpp`; use Lambda/lib numeric and UTF kernels underneath.
- Public classes extend existing class plans, nominal runtime classes, home
  objects and closure environments. Preserve Lambda ancestry and JS reference
  captures (**S2.1.3v2**, **D2.6.6v3**, **D2.6.10**, **D6.2.3v2**).
- Template arrays reuse ordinary arrays and existing descriptor/integrity
  operations. Retain site-owned values through the execution owner's existing
  precise metadata roots, not an untraced static cache.
- No is_normal flag, unconditional environment allocation on ordinary calls,
  second interpreter, graph-copy bridge or whole-unit fallback. Retain proven
  native paths and guard generic semantic operations (**D8.4.1v2**).

Search for existing helpers before adding one. Record and announce each concrete
new production helper before coding; small options preserve existing callers.

## 4. Validation and closeout

Freeze an exact-entry release-profile control. Require focused normal/forced-GC
tests, canonical family and whole-target results, zero lost entry passes after
individual replay, and unchanged full-LJS/Lambda baseline gates. Restore the
ordinary release executable after the debug aggregate.

Use the existing paired benchmark runner with pinned MIR, script self-times,
matching source/output contracts and an identical control copy. Check all 84
non-jq contracts, screen normal array/map/object/call/class workloads, and repeat
suspected regressions. Keep all four jq workloads excluded from execution,
preflight and timing. Preserve §32's Fasta/noisy-screen and older phase-control
gaps until separately resolved; a new control does not erase them.

Report the 5,000-ID milestone separately from completion of the five feature
areas. Retain crashes, timeouts, missing outcomes and host-contention evidence.
No Result-series publication or commit is part of this task.

## 5. Implemented changes and disclosed helpers

| Area | Reuse and concrete additions |
|---|---|
| Objects/metadata | Existing primitive boxing, descriptor conversion, own-property definition and lazy function preparation; method catalog owns arity/name metadata and own-key order. No new descriptor kernel. |
| Arrays | `mvp_lmd_array_visit_method` shares method classification; existing visitors/index/species operations now handle generic receivers, inherited holes, reverse reduction and object/proxy species results. |
| Templates | `js_build_template_object_with_ops` extracts LJS's builder; `mvp_lmd_template_object` supplies MVP arrays/descriptors and caches each site in traced program slots. |
| Strings/primitives | `js_string_raw_with_ops` and `js_string_from_codes_with_ops` extract shared LJS algorithms. `primitive_static` and `string_method` adapt existing numeric/UTF/string kernels. Existing padding accepts an end-padding option. |
| Classes | `class_expression` lowers fresh local/computed/field/static-block evaluations. Existing new-function, class-new and synthetic-initializer helpers accept class-owner/static-block options. `js_function_name_display` extracts LJS name formatting; `mvp_lmd_function_set_name` and `named_evaluation_key` connect inferred/computed names. |
| Globals | `plan_global_bindings` and `mvp_lmd_global_initialize` connect observable object bindings to the global Map while keeping lexical TDZ bindings separate. Existing primitive dispatch supplies coercing global numeric operations. |
| Grouped calls/tags | `capture_member_callee` shares reference capture for ordinary/spread calls and template tags, retaining successful receivers and ending grouped optional chains before argument/substitution evaluation. |
| Prototype lookup | `get_prototype_with_library` extracts the existing getter. Existing inherited-member and property-cache helpers accept/reuse resolved metadata and immutable shape slots; every lookup still reads the current chain and values. |
| Global exposure proof | `global_bindings_are_exposed` is a disclosed compiler helper; it reuses the promoted `js_ast_fact_walk_skips_child` classifier and native shape inventory to retain local key proofs when global receiver uses cannot reach bindings. |

These production helpers were disclosed before coding. Shared algorithms use
precise roots and existing Lambda/lib storage; ordinary proven calls, literals
and class construction retain their direct paths. No harness or vendor edits.

Additional root-cause repairs found during validation:

- String sequence construction now coerces once in source order and builds
  bytes linearly; existing UTF-8 canonicalization joins adjacent surrogates.
- Publish actual trim aliases, prototype toFixed and Symbol.unscopables; retain
  Symbol's throwing native [[Construct]] entry for valid class heritage.
- Shared early-error checks reject escaped reserved labels and ungrouped optional
  template tags; empty labelled bodies are null-safe. Template-tag receivers
  escape scalar replacement just as ordinary method receivers do.
  Escaped `await` remains legal as a label in strict, non-async scripts;
  async/module contexts retain their rejection.
- LJS's String.raw adapter checks object coercibility before legacy boxing,
  whose general helper intentionally accepts null/undefined.
- Reserved Array stores and proven aligned Object fields omit unreachable MIR
  fallback blocks. Required dynamic/unaligned/float paths remain. The Hyphen
  module drops from 714,874 lines / 19.1 MB to 355,368 / 9.58 MB; this measures
  emitted code size, not a script-time speedup.
- Primitive method admission distinguishes Array element reads from writes to
  primitive prototypes. This retains direct ordinary string calls while actual
  prototype/constructor mutations still invalidate the proof; it removes the
  intermediate JSON slowdown.
- Ordinary Set dispatch precedes class-definition attributes. Inherited reads
  reuse resolved class/library metadata and shape-cache slots for data fields.
  Private prototype-link slots reuse the same cache representation. Only immutable
  shapes are cached; links and values stay live, accessors and mapped arguments
  retain semantic dispatch, and no managed value is added to an untraced cache
  (**D2.6.10**, **D5.2–D5.3**, **D8.4.1v2**).
- Reflected global reads reuse the existing property-cache emitter with a
  caller-owned miss branch. Cache misses retain ordered semantic HasProperty/Get,
  inherited proxy traps and missing-name ReferenceError. The new proxy check also
  exposed and repaired an older use of the low-level own-property presence helper.
  Global reflection and its conservative type/coercion proofs remain enabled.

## 6. Acceptance evidence

Artifacts are under `temp/mvp_test262_5000/`. The exact-entry control is HEAD
`1df68afe36d8fb020e9d6088d544784ffa574028`, release-profile SHA-256
`dcc42a1059769edf2d6a27185c20c9b5b0eb08dfb77a069e8105119a95543a96`.
The current candidate23 release-profile SHA-256 is
`e2804254cf893b003aa38397514bd93426227cdb8ac454fe049e17cf3cb2f95f`.
Each pin retains source hashes and a manifest; intermediate candidates remain
separate and their counts are never combined into a single-build milestone.

| Gate | Current evidence |
|---|---|
| Focused MVP | **193/193**, final23/normal.log |
| Forced GC / freed-memory poisoning | **193/193**, final23/forced_gc.log; exact hashes and retained test executable in candidate23/ |
| Shared full LJS target | **40,261/40,261**, zero unresolved; final23/ljs-final/outcomes.json |
| Exception / callable catalogs | Both pass, final23/catalog-gates.json |
| MVP whole target, selected mode | **21,748/40,261**; **3,977 gains, zero lost entry passes**, no new crashes; final23/final-inventory-summary.json |
| Required-mode audit | **21,747 distinct IDs**: 19,221 positive + 2,526 negative; final23/audited-summary.json |
| All 84 non-jq contracts / paired self-times | Candidate23: **84/84** contracts; 36 workloads screened, 10 repeated for 31 rounds; earlier performance gaps remain open |
| Lambda baseline / release restoration | **6,806/6,806**, including 2,112 input checks; final23/lambda-baseline.log. Ordinary release restored successfully; final23/aggregate-gates.json. |

### Counting and comparison

The canonical harness emits one metadata-selected mode per ID. Of the frozen
40,261-ID target, 37,098 require both sloppy and strict execution; 2,229 are
noStrict, 611 onlyStrict, 292 modules and 31 raw (one raw/module overlap).
Report canonical-mode results separately from the required-mode audit. An
isolated metadata overlay selects the additional strict variant through the
unchanged runner; source/includes, negative phase/type expectations and async
policy remain exact, and the original metadata is untouched. Count an ID toward
the milestone only after all required modes pass on the same pinned candidate.

Entry's complete canonical-mode inventory plus isolated replay records
**17,771 PASS, 22,210 FAIL, 270 CRASH, 10 TIMEOUT**. Candidate23 records
**21,748 PASS, 18,251 FAIL, 254 CRASH, 8 TIMEOUT**. It gains **3,977** selected-mode
passes, loses no entry pass and introduces no new crash ID. Every ID has an
explicit final outcome. Four entry failures now time out: Map/Set `valid-keys`,
String `toLocaleUpperCase/special-casing`, and property accessors `S11.2.1_A4_T9`.
These remain unsuccessful outcomes; the Map/Set sources also depend on
unsupported WeakMap/WeakSet/WeakRef globals. No timeout is counted as a pass.

The additional strict audit passes **20,203/20,204** variants; with **1,544**
single-mode passes, the same candidate establishes **21,747 audited distinct
IDs** (**19,221 positive + 2,526 negative**). The sole strict failure is
`language/global-code/S10.1.7_A1_T1.js` (`delete this`), which also fails on entry.
The previously failing escaped non-async `await` label now passes.

Candidate23's bulk census used four workers and 50-ID batches. After timing
finished, a two-worker strict prefetch ran on the anticipated required-mode IDs
using the same pin. Reuse verified exact executable, runner, corpus, metadata,
source and requested-ID hashes. Original outcomes/logs and the separate reuse
record remain under `final23/`. All 77 unsuccessful or unresolved strict outcomes
were replayed individually. Individual replays retain one-ID batches and the
original 10-second timeout. Three apparent lost entry passes under concurrent
census/strict work passed in a quiet serial replay before the audit was accepted.
These are runner scheduling options, not changes to the canonical harness or test
contents. The full Lambda build began only after the final quiet replays ended.

| Selected family | Entry passes | Candidate23 passes | Gained |
|---|---:|---:|---:|
| Object / Reflect / Function | 3,412 | 3,554 | 142 |
| Array | 1,808 | 2,618 | 810 |
| Class declarations / expressions | 1,555 | 3,056 | 1,501 |
| String / Number / Boolean | 631 | 1,296 | 665 |
| Tagged/template literals | 26 | 38 | 12 |

The table is a subset of the whole target, not an additional pass count.

### Final pinned performance capture

All 84 non-jq source/output/backend contracts passed. The unchanged paired
runner screened 36 workloads for five rounds, then repeated 10 positive/noisy
differences for 31 rounds. Both controls use the exact entry executable. Times
are script-reported milliseconds on pinned release MIR, excluding compilation.
`final23/performance-summary.json` retains both paired 95% intervals per row.

| Workload | Candidate | Entry | Entry peer | Interpretation |
|---|---:|---:|---:|---|
| Hashmap | 470.229 | 477.287 | 478.058 | 1.5–1.6% lower screen medians; intervals cross parity |
| CD | 159.378 | 159.232 | 159.074 | 31-round intervals cross parity |
| Havlak | 1758.027 | 1750.158 | 1744.871 | +0.45–0.75%; one interval crosses parity |
| JSON | 5.577 | 5.594 | 5.518 | Both 31-round intervals cross parity; earlier ~22% regression repaired |
| Binarytrees | 3.839 | 3.857 | 3.859 | Initial positive screen difference did not reproduce |
| Navier–Stokes | 34.105 | 33.428 | 33.429 | **+2.0%** medians; both 31-round intervals cross parity |
| Text search | 5085.898 | 5074.678 | 5144.064 | Mixed screen; both intervals cross parity |

The candidate23 phase-completion capture was inconclusive: no row had both
paired 95% intervals wholly above 1, but its wider intervals did not clear
candidate21's reproduced Navier–Stokes **+3.0–3.2%** and text-search
**+0.22–0.28%** differences (`final21/performance-summary.json`). The follow-up
below resolves the phase33 Navier difference; the user deferred the small
text-search difference. Older phase gaps remain separate. Samples placed nearly
all Navier execution in numeric JIT code; the follow-up identifies conservative
admission as the cause and restores local proofs while retaining global reflection.

Root-cause evidence and rejected experiments are recorded in
`final23/tuning-analysis.json`. No fresh jq execution or Node comparison was run.

### Navier–Stokes regression follow-up (2026-10-13)

A fresh 31-round paired capture confirms the candidate23 regression:
**33.305 ms vs 32.236 / 32.360 ms**, or **+3.32% / +2.92%**. Both paired 95%
intervals exclude parity; the duplicate entry controls agree within uncertainty.
Evidence: `temp/mvp_navier_regression_20261013/recheck/comparison.json`.

The checksum's sloppy `this` correctly activates reflected global bindings.
Treating every such receiver as an escaped global object also rejects local
computed-key proofs, admitting callbacks throughout unrelated numeric kernels.
The compiler now separately proves whether receiver uses can expose bindings:
`globalThis`, unresolved names, escaped/computed receivers, native keys and
same-spelled global properties retain conservative admission. Distinct static
user fields reuse the existing local parameter/store inventory. This is a
compile-time proof under **D8.4.1v2**, with the original runtime property kernels
and global bindings retained. Focused cases cover each rejected exposure route.
The patched release SHA-256 is
`f2b5e7c51c057836000bb6860b1bbd3c70be1b042ca72b039eab8896176a8a5e`.
The final 31-round capture gives **31.978 ms vs 32.435 / 32.450 ms**:
**1.41–1.45% faster than the phase entry**, with paired 95% ratio intervals
**[0.98040, 0.99044] / [0.97914, 0.98783]**. Both intervals exclude parity;
the duplicate controls' interval includes parity. Frozen script and density
oracle are identical to the reproduction. Evidence: `fixed-final/comparison.json`
and `performance-summary.json` under the follow-up directory.

Both normal and forced-GC/freed-memory-poisoning gates pass **194/194**.
The hot `lin_solve` body drops from 11,865 to 11,558 MIR lines; the semantic
array-index adapter is absent again. This line count is structural evidence,
while the script times establish the performance result. All **84/84** benchmark
output/backend contracts and **6,814/6,814** shared Lambda checks pass. The full
LJS Test262 baseline passes **40,261/40,261**, with zero regressions, using the
same pinned executable. Ordinary release restoration also succeeded; gate commands,
exits and hashes are in `gates.json`. The 21,747-ID audit above remains
attributed to candidate23; this targeted follow-up does not relabel that audit.
Preserved diagnostic failures include an early replay during linking, a copied
gtest executable missing its module search path, and an off-by-one test fixture;
none is counted as a passing gate.

The user deferred the tiny text-search difference; this follow-up does not repeat
or tune its timing comparison. The four jq workloads remain excluded.

### Preserved failed/intermediate evidence

- `entry-inventory` was interrupted during the full-LJS-baseline automatic
  all-failure retry; the replacement uses explicit frozen IDs and selected
  isolated replay. No milestone count uses the interrupted run.
- Overlapping `candidate-exact` increased contention/lost-batch outcomes and was
  stopped with an interruption record. Final timing runs do not overlap
  conformance work; candidate23's census and strict prefetch share one pin.
- Candidate1 passed 83/84 non-jq contracts; Hyphen hit its 180s startup limit.
  Candidate3 passed Hyphen under contention (113.2s wall, checksum 1,183,296),
  while entry took 138.3s wall. These are diagnostic, not performance comparisons.
  An ad-hoc candidate4 replay retained only a boolean failure and establishes
  neither timing nor failure classification. Quiet final capture is required.
- Candidate8 had 21,746 audited IDs on its own pin (19,220 positive + 2,526
  negative). Its separate evidence is retained; it is not combined into the
  candidate23 milestone.
- Candidate7's shared LJS gate passed 40,259/40,261; the two String.raw nullish
  failures led to the coercibility fix and candidate8's complete passing rerun.
- Candidate18's new inherited-global/proxy test also failed on candidate17. The
  low-level presence helper was replaced with existing semantic HasProperty
  dispatch in candidate19; final21 retains the repair and cache-miss order check.
- Candidate20 put Lambda's signed-zero decoder before generic numeric coercion.
  It worsened Navier–Stokes and was reverted; the additional signed-zero,
  subnormal and effect-order coverage remains. No `coerce_nonzero_number` helper
  remains in the implementation.
- Candidate21's full census and isolated replay retained 21,747 selected-mode
  passes but lost `String.prototype.replace/regexp-capture-by-index` to the
  unchanged 10-second timeout. Quiet entry and candidate17 replay passed. A
  diagnostic run isolated the global-read cache expansion: MIR grew from
  125,017 to 163,863 lines and wall time from 7.1 to 12.6 seconds. Inline global
  caches are now emitted only inside functions or loops; straight-line script
  reads retain semantic HasProperty/Get without a cache decoder. The existing
  focused group checks both emission paths. No new helper or timeout change.
  Candidate23 has 129,640 MIR lines and takes 7.4 seconds in the same diagnostic;
  the canonical quiet replay passes. A replay overlapping linking timed out and
  remains recorded separately. Candidate22's temporary MIR assertion incorrectly
  searched for register names omitted from release dumps; it was corrected to
  check the shared decoder import, and candidate23 passes all 193 focused groups.

## 7. Remaining boundaries

This phase extends existing operations; it does not claim full conformance for
all Object/Array/String families. Full RegExp symbol protocols, remaining Array
operations such as flatMap, private class elements, builtin subclass instance
slots, interpreter interoperability, async/modules and advanced buffers remain
outside completion claims. Eval/Function retain **D8.1.3v24**'s interpreter
policy. Dynamic class captures and metadata share the existing 255-slot closure
environment limit. All unsupported/failed canonical IDs stay visible in the inventory.

The numeric milestone, feature scope and performance gates are reported
separately. Carry forward §32's unresolved Fasta/noisy-screen and older
phase-control differences; comparison to a new entry control does not resolve
them. The four jq workloads remain excluded from execution and retain historical
evidence only. No published Result record is changed by this phase.

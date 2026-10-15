# JS MVP: complete the supported LJS Test262 population

**Date:** 2026-10-16. **Status:** In progress; full parity is not established.
**Design:** [JS_MVP_Lmd §35](../jube/JS_MVP_Lmd.md#35-complete-ljs-test262-parity).

## 1. Target and evidence

Pass every ID in the frozen 40,261-case LJS population, in every required mode,
on one pinned MVP release build. Preserve the corpus, harness, includes,
negative phase/type expectations, deadlines and baseline exclusions. This
continues the [parity plan](JS_MVP_Lmd_Test262_Parity.md) and
[90-percent plan](JS_MVP_Lmd_Test262_90.md), without resetting their open gates.

The latest completed whole audit remains archived pin38: 37,445/40,261
all-required passes, 2,816 unresolved IDs. The later pin46 selected record
passes 587 of those IDs, including all 103 archived SharedArrayBuffer failures.
These different-build counts are not additive. Entry source is clean HEAD
`614fbc5d9f76e520a75df3acae153101f35d0127`; its 1,454 production input hashes
match the existing release control `05e9ae4f3d95576d7ca4e007477b94b8d02dd84520a9377889af3578f0ff8416`.

## 2. Contracts

- **D1.3v3:** reuse untyped Lambda and shared JS algorithms first. Add explicit
  operation profiles where property/callable/error representations differ;
  never reinterpret incompatible callable records or copy an object graph.
- **D8.1.3v24:** direct/indirect eval, Function-family constructors and dynamic
  bodies remain in the retained AST interpreter. Complete live environment
  and host-entry integration; do not replay ordinary unsupported MVP scripts.
- **D3.4.3v5–D3.4.8, D5.2–D5.3:** preserve shared shapes, scalar ownership,
  precise roots and explicit completions through observable callbacks.
- **D8.4.1v2:** preserve immutable generated code and guarded specialization;
  no inline caches or universal `is_normal` flag.

## 3. Work packages

| Package | Implementation and completion condition |
|---|---|
| P0: inventory | Refresh canonical and required strict outcomes on a private pin. Classify current failures by root cause; retain prior passing controls and crashes/timeouts explicitly. |
| P1: library surface | Complete replaceAll, Number formatting, BigInt width operations, legacy escape/accessor methods and Object.fromEntries through existing kernels. Cover descriptors, coercion order, symbols, IteratorClose and scalar ownership. |
| P2: syntax and environments | Complete shared source validation for statement positions, declarations, bindings, literals and nested await/yield contexts. Close remaining with/eval, global lexical and module-eval integration. Negative tests must fail in their declared phase/type. |
| P3: RegExp | Finish Unicode sets, property escapes/validation, protocol interactions and large-pattern resource handling using the existing frontend/matcher infrastructure. |
| P4: JSON and exotics | Share reviver/replacer/indentation algorithms, then finish array iteration/species/subclass and proxy/accessor interactions. Preserve ordinary JSON and array fast paths. |
| P5: Atomics/host | Adapt existing atomic storage, waiters, promises/jobs and applicable Test262 agent operations. SharedArrayBuffer is already implemented. Complete applicable evalScript/realm facilities without broadening baseline scope. |
| P6: weak references | Implement WeakRef and FinalizationRegistry constructor/brand/prototype and registration operations. Audit weak lifetime and cleanup ownership against existing collector/job facilities; WeakMap/WeakSet already have baseline coverage. |
| P7: integrated closure | Complete remaining compiler/runtime failures. Require all 40,261 IDs in required modes, no lost prior passes, shared baseline gates and benchmark performance acceptance. |

Packages stay open until their current-build results and integration controls
pass. Selected-family gains never establish a new whole-suite score.

## 4. First implementation increment

P1 reuses the numeric formatter, BigInt modulo operations, URI/legacy escape
implementations, replacement/substitution loop, collection iterator contracts
and property descriptor kernels. Primitive conversion and error construction
stay in the owning frontend. Replacement matching and callback offsets retain
UTF-16 semantics; primitive search values do not consult Symbol.replace.

Shared entry points were announced before implementation:

- `js_object_legacy_accessor_with_ops`: consolidates existing define/lookup
  algorithms, with a small property-operation profile. A data descriptor
  stops prototype lookup, and inherited descriptor-object fields are ignored.
- `js_object_from_entries_with_ops`: reuses collection iteration/closing and
  the existing property-construction operation table. Read entry 0, entry 1,
  then convert the key; root values through every callback and close only the
  operations for which IteratorClose is required.
- `js_json_revive_with_ops`: promotes the existing bottom-up reviver walk.
  Snapshot keys, preserve callback order and scalar homes, and ignore refused
  deletes/definitions while propagating abrupt completions.
- `js_json_stringify_with_ops`: shares replacer/property-list processing and
  recursive serialization. Process the property list before spacing; truncate
  spacing in UTF-16 units, retain embedded NULs, and preserve toJSON/replacer
  order. Reuse Lambda's shortest-round-trip formatter (**S4.8.1**). Ordinary
  MVP serialization retains its existing fast path.
- `mvp_lmd_named_set` gains a canonical-key overload, consolidating built-in
  symbol-tag descriptors and preserving scalar ownership through definition.

P2 extends the existing shared statement walker with a statement-position
option. Lexical/class declarations require a statement list; Annex B permits
ordinary function declarations only in eligible sloppy if/label positions.
The same validator serves MVP and LJS (**D8.1.3v24**).
Shared binding checks also reject lexical/var collisions and async/generator
duplicates; sloppy labelled ordinary functions retain their Annex B binding.
Object shorthand requires an IdentifierReference.
Reflect methods now use their declared function names and the shared intrinsic
descriptor setter; their previous initializer omitted all 13 function names.

P4 uses Lambda's strict JSON parser and the shared callback walks. LJS retains
its existing source-context extension. MVP parse-with-source/rawJSON are outside
the frozen population and are not claimed by this increment.

## 5. Verification and performance

Retain private executable hashes, tracked input guards, raw outcomes, commands
and source hashes under `temp/mvp_full_parity_20261015/`; canonical harness
captures remain under `temp/mvp_test262_90/full_parity*/`.

Run focused semantic checks normally and under forced/poisoned GC, selected
canonical/strict families, the unchanged LJS baseline and Lambda baseline.
Failures in shared gates need exact control replays before attribution.

Benchmark release MIR binaries with unchanged workload/output contracts,
alternating control/candidate order and self-reported times. Check affected
families before the complete supported sweep. The earlier Microdiff 6.8%,
DeltaBlue 4.1% and Hashmap 2.2% historical gaps remain open; this phase must not
rebase them away or introduce new regressions. Do not claim performance
acceptance from implementation inspection or semantic results.

Timing provenance: preserve each workload's archived contract. The prior60
rows use their exact CLI entry execution timer; newer script-timed rows use
their own timers. Both are self-reported execution times with startup/JIT
excluded, and process wall time is recorded separately. The captures retain
the per-row contract rather than treating these as interchangeable timers.

### Initial integrated evidence (round8)

Private round8 release SHA-256:
`6f26e58636e2544188619c5b7f2b43248258485dc2cf143eb654b25d80b9133c`.
All 1,454 tracked production inputs remained unchanged during its build.

- MVP unit checks: **674/674**; the seven new checks also pass with
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
- Selected Test262: **541/541 canonical**, **527/527 additional strict**;
  **541/541 LJS controls** on the same binary. Includes the frozen JSON family.
  The exact entry control passes 170/541 canonical cases: **371 recovered,
  zero lost passes** in this cohort, without implying a whole-suite score.
- An earlier round4 replay of the archived 2,816 residual IDs recorded
  **1,081 pass, 1,731 fail, four timeouts**, after isolated replay of missing
  worker results. It is diagnostic evidence, not a current whole-suite score.
- `make test262-baseline`: **40,261/40,261**, zero regressions, with the private
  round8 release selected through `LAMBDA_TEST262_EXE`.
- Lambda baseline retains four independently reproduced entry failures after
  the JSON golden correction below. Final release performance acceptance
  remains open.

Selected captures: `temp/mvp_test262_90/full_parity8/`. Unit checks and source
guards: `temp/mvp_full_parity_20261015/round8/`. The nullish receiver regression
found in the shared accessor refactor was corrected before these final selected
captures; its four exact entry controls passed.

The shared serializer also removes LJS's `%.17g` decimal spelling. The two
affected lines in `test/js/lib_jsonpath.txt` now use the shortest spelling;
all 40 actual fixture lines match Node independently, and the exact GTest
replay passes. This is an oracle-backed output correction, not a Test262
baseline or harness change. Evidence: `round8/jsonpath-control.json`.

The initial Lambda aggregate was **7,449/7,454**. After the independently
verified JSON golden correction, the four remaining failures reproduce on
the exact entry build: three PDF page-fragment checks and the chart wordcloud
golden. The PDF replay covers interpreter, JIT and auto; the two chart outputs
are byte-identical. The original aggregate result is retained, and only the
JSON check was replayed after its golden correction. The integrated GC stress
sweep passes **313/313**.

### Performance repairs during this increment

The round8 sweep covered **84 non-jq workloads**, seven measured paired rounds,
on pinned release MIR binaries. All output/backend/source guards passed. Four
jq timings remain deferred by user instruction. The aggregate geometric mean
was +0.22%, but repeat measurements confirmed object-growth and three-way-merge
slowdowns; the aggregate did not satisfy the performance gate.

Native sampling and the generated MIR identified two avoidable runtime costs:

- `Object.values` re-interned idless field names even when no keys or callbacks
  were needed. Enumeration now uses the shared key classifier and canonical
  name metadata for ordering, materializing names only when consumed. Immutable
  shape-owned spellings remain valid across callbacks (**D3.4.3v5**).
- Literal `String.split` traversed the general RegExp protocol entry despite
  already having string operands and an absent limit. That case now enters
  the existing rooted Lambda split kernel directly. Observable conversion and
  object protocol operations retain the general path (**D5.2–D5.3**).

Round10 eleven-pair repeats measured object growth **21.6% faster** than the
entry build and three-way merge **+0.11%**, with its 95% interval crossing
parity. Richards remained **+0.63%** in that repeat, so performance acceptance
stays open until the final sweep is reviewed. Final round11 evidence follows
separately; measurements from different pins are not combined.


### Final increment validation (round11)

Release SHA-256:
`bed44dcc9399000d94f9cb43c29b6d5503bd2a37bbfcade9aa40690eb5dd7e68`.
All 1,454 production inputs matched its build manifest.

- **675/675** MVP unit checks; **8/8** new checks with collection forced on
  every allocation and freed storage poisoned. The projection-ordering and
  split-coercion checks also pass under those GC settings.
- **786/786** canonical MVP cases and **772/772** additional required strict
  variants; **786/786** LJS controls. The original 541-case comparison cohort
  retains **371 recovered, zero lost** canonical passes versus entry.
- Reflect's 13 method-name checks are included. The omitted names were also
  reproduced on the exact entry release before fixing the initializer.
- The full LJS baseline passes **40,261/40,261**, zero regressions, using
  this private release.

Captures: `temp/mvp_test262_90/full_parity11/` and
`temp/mvp_full_parity_20261015/round11/`. The whole MVP population has not been
recounted. RegExp, Atomics/agent, weak-reference and remaining environment/host
packages remain open; the current increment does not establish full parity.

Round11 performance: **84/84** non-jq output/backend checks pass; the seven-pair
sweep is **0.55% faster** by geometric mean against phase entry. Object growth
is **22.4% faster**; three-way merge is **-0.44%**, with its interval crossing
parity. Primes and triangl received 15 more pairs after initial small positive
intervals; the combined 22 same-pin pairs have intervals crossing parity for
both. No new slowdown is confirmed in this capture. This does not close the
three documented historical gaps or prove unaffected performance for every
program. All source/binary guards match. Full tables and separate repeats:
`temp/mvp_full_parity_20261015/REPORT.md`.

## 6. RegExp increment and corpus provenance

P3 now admits `/v` through the existing shared Unicode-set lowering, then uses
the existing backtracking matcher. Matching and empty-match advancement treat
both `/u` and `/v` as code-point modes; public flag getters remain distinct.
The matcher accepts property escapes inside classes, rejects unknown Unicode
properties, and uses shared Unicode simple-fold cycles for literals, classes,
backreferences and word boundaries. Vendor code is unchanged.

The new core helper `utf8_simple_fold_next` reads a generated table from the
repository's existing Unicode 16 `CaseFolding.txt`; the generator accepts only
common/simple mappings. It performs no normalization or multi-character full
folding. This fixes the Greek and ligature pairs absent from the older RE2
table without copying LJS's normalization heuristic or changing the property
tables to Unicode 17. Regenerate/check with
`python3 utils/generate_unicode_casefold.py lmd/package/latex/tex/CaseFolding.txt --check`.
Forward and lookbehind backreferences decode both operands in matching order;
case-equivalent characters can occupy different UTF-8 widths. Native byte
lengths cannot determine the subject boundary for an ignore-case backreference.

The shared early-error pass validates literals before execution, including
unreachable literals. It uses the existing capture-name decoder and RegExp
frontend; constructor patterns remain runtime checks (**D8.1.3v24**).
One new internal helper, `validate_unicode_property_escape`, shares braced-name
validation between the class and non-class paths. It reuses existing generated
property-name tables and admits properties of strings only for positive `/v`
escapes. Character-class escapes cannot be character-range endpoints.
The shared validator also enforces `/v`'s reserved punctuation grammar
([ECMA-262 ClassSetCharacter](https://tc39.es/ecma262/multipage/text-processing.html#prod-ClassSetCharacter)).
Legacy malformed control escapes preserve their literal backslash; the existing
control parser takes a class-context option for Annex B digit/underscore cases.
Negated `/v` classes reuse the set parser's `MayContainStrings` facts during
early validation. This tracks syntax rather than just the computed set:
subtraction retains the left operand's possibility of containing strings,
including when subtraction removes every concrete string.

The interpreter host operation profile now includes a RegExp-literal factory.
MVP supplies its existing native constructor kernel, preserving realm identity
without translating dynamic bodies to MIR or copying LJS exotic records
(**D8.1.3v24**, **D1.3v3**). Direct/indirect eval and Function-created bodies
retain AST execution. The optional shell host also exposes `print` through the
existing rooted output handler.

Round12's canonical RegExp comparison records **1,442/1,721** passes versus
**1,310/1,721** on round11: **132 recovered, zero lost**. Both retain one
timeout. This diagnostic precedes the case-folding and early-validation fixes;
final validation and performance acceptance for the RegExp increment remain
open. Captures: `temp/mvp_test262_90/full_parity12/`.

### Distinct corpus versions

The standard `make test262-baseline` gate uses stored test bodies under
`test/js262/test`, including Unicode **16.0** property tests. MVP canonical
mode uses `ref/test262/test`, whose corresponding cases require Unicode
**17.0**. The same case IDs do not establish identical source contracts.
The earlier **40,261/40,261 LJS baseline** results are valid for the standard
stored-body gate, not a claim that LJS passes all current canonical sources.

Direct canonical controls reproduce failures in both engines for Cased
U+A7CE, Emoji U+1FAC8 and Currency_Symbol U+20C1. The shared generated RegExp
tables still contain Unicode 16 data. The canonical target remains open;
these failures must not be hidden by substituting stored bodies or adding
exclusions. Updating shared Unicode data also requires addressing the older
baseline's incompatible non-match expectations explicitly. Neither corpus
nor the Test262 harness has been modified. Source hashes and direct controls:
`temp/mvp_full_parity_20261015/regexp-entry/corpus-provenance.json` and
`generated-diagnostics13.json` in the same directory.

Before the final parser/host additions, round15 passes **1,601/1,721 canonical**
RegExp cases and **1,599/1,719 additional strict** cases. Against round11's
canonical control, **291 passes are recovered and none lost**. Explicit
`--no-stripped` LJS controls pass **1,650/1,721** on that same round15 binary:
71 failures are shared, and 49 are still MVP-only at that point. This confirms
why stored-body baseline results cannot establish canonical parity. Round15
also passes **676/676** MVP units, **9/9** new forced/poisoned-GC checks and
**51/51** shared backtracking-matcher units. The final semantic checks for the
newer additions are captured on round21. This pin passes
**677/677** MVP units, **10/10** focused forced/poisoned-GC checks and
**51/51** shared backtracking-matcher units.
An exhaustive data-driven check matches all **1,484** common/simple UCD mappings
in both directions and rejects all **106** full/Turkic mappings, independently
matching Node. Canonical RegExp coverage reaches **1,649/1,721** after isolated
replays (**339 recovered, zero lost** versus the round11 control). The original
missing worker outcomes and remaining timeout are retained in the captures.
Additional strict variants pass **1,647/1,719**; all required modes pass for
the same 1,649 canonical IDs. Canonical LJS passes **1,650/1,721**: the sole
MVP-only semantic failure is the cross-realm hasIndices case. There are 71
shared non-passing IDs, including MVP's remaining Unicode-property timeout.
The library cohorts pass **786/786** canonical and **772/772** additional strict
variants. The full stored-body LJS baseline passes **40,261/40,261**.
The final integrated Lambda baseline records **7,453/7,457**, with the same
four entry-reproduced PDF/chart failures described above. All JavaScript groups
and all **313/313** forced-GC stress checks pass. Aggregate captures are retained
in `round21/gate-artifacts/`; no baseline expectations were changed.

Round21 release:
`65849a5ac8f5485193eec39aec6101a0879be0f0c099d9e02b674ba4f50b3680`.
All 1,455 production inputs match. Its 84-workload non-jq performance sweep
passes every output/backend contract and has a **-0.47%** geometric mean versus
phase entry. Five workloads received 15 additional paired samples; the combined
22-pair intervals cross parity for NBody, revcomp and raytrace3d, and include
parity for args_ctl. After the Lambda baseline exited, an independent 31-pair
`object_delete` repeat measured **0.867 vs 0.836 ms**, **+3.7%**, with a paired
95% interval of **+1.79% to +5.64%**. The slowdown is confirmed; performance
acceptance remains open. All 53 same-pin pairs are retained, alongside the
original captures and the earlier 22-pair summary.

Its MIR differs only in 13 pointer immediates, with identical instructions,
guards and helper calls. Native sampling of a longer diagnostic loop shows
the same shape-transition/name-comparison path in both builds. The symbol-map
binary has the same executable text-section hash as the measured candidate.
The shared mutation helpers' source is unchanged; native code placement remains
a hypothesis, not an established root cause. No padding or unrelated tuning
has been applied to conceal the regression. Diagnostic samples/disassembly:
`temp/mvp_full_parity_20261015/delete-profile21/`. Captures and complete tables:
`temp/mvp_full_parity_20261015/REPORT21.md`.

### Remaining host and lifetime audit

The P5/P6 implementation must reuse sound shared storage and collector seams
(**D1.3v3**, **D5.2–D5.3**), not infer completion from LJS's API presence:

- LJS's Test262 agent currently executes callbacks synchronously. Its Atomics
  adapter contains synthetic spin-loop values and report-string substitutions.
  MVP must implement actual agent scheduling, shared storage visibility and
  waiter completion before exposing these operations; those substitutions are
  not reusable semantics.
- LJS's WeakRef target and FinalizationRegistry cells currently occupy strong
  hidden properties. The shared collector already exposes weak slots and
  ephemerons; P6 needs a native weak carrier, job-scoped kept-alive ownership,
  and cleanup-job scheduling. Merely forwarding the existing constructors
  would retain targets strongly.
- Hosted `$262.evalScript` needs persistent global lexical declarations and
  Script declaration attributes. It cannot alias indirect `eval`, whose
  lexical declarations are private to that evaluation. Hosted realm creation
  also needs distinct intrinsic identities and callable ownership.

These packages remain unimplemented in this increment. The Unicode 16 versus
17 target decision is pending separately; it does not redefine these gaps.

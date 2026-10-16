# JS MVP: complete the supported LJS Test262 population

**Date:** 2026-10-16. **Status:** In progress; full parity is not established.
**Design:** [JS_MVP_Lmd §35](../jube/JS_MVP_Lmd.md#35-complete-ljs-test262-parity).

## 1. Target and evidence

Pass every ID in the frozen 40,261-case LJS population, in every required mode,
on one pinned MVP release build. Preserve the corpus, harness, includes,
negative phase/type expectations, deadlines and baseline exclusions. This
continues the [parity plan](JS_MVP_Lmd_Test262_Parity.md) and
[90-percent plan](JS_MVP_Lmd_Test262_90.md), without resetting their open gates.

The latest completed whole audit is the intermediate atomic-storage pin in
§7: **39,266/40,261** all-required passes without observed instability.
Archived pin38 recorded 37,445/40,261 passes, 2,816 unresolved IDs. The later pin46 selected record
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

### Host and lifetime audit at round21

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

These packages remained unimplemented at round21. The Unicode 16 versus
17 target decision is pending separately; it does not redefine these gaps.

## 7. Lifetime, Script, realm and atomic-storage continuation

Continuation entry is clean merge `b4d41669380056e53243dd084cb0a9044a6611b7`.
The private release control is
`0b1d4341cdb2162b1a8e183a5428275d28ac4b3ef03905c3a4866d63455c15f8`;
2,941 production inputs remained unchanged during its build. Captures are in
`temp/mvp_full_parity_next_20261016/`. This entry is distinct from round21's
older phase control and does not erase its outstanding performance gaps.

### P6: shared weak lifetime storage

WeakRef and FinalizationRegistry now share a native Lambda Map carrier with
an ordinary shape/property face. `MAP_KIND_WEAK` selects its payload; there is
no new TypeId or ordinary-container flag. Both JS front ends use the same
validation, registration, dereference and unregister algorithms (**D1.3v3**).

The collector traces callbacks and holdings strongly, registers targets and
unregister tokens as per-collection ephemerons, and records pending cleanup
without invoking guest code. WeakRef construction/dereference retain their
targets in a job-scoped RootVector. An outer microtask checkpoint releases
that set and invokes pending cleanup with rooted carriers and destination-owned
scalar holdings. Nested synchronous calls cannot clear the set. Native records
unlink on collection and detach before runtime destruction; batch reset drops
the preceding realm's state (**D5.2–D5.3**).

Announced shared helpers: `js_weak_validate_input`,
`js_weak_create_with_storage`, `js_weak_method_with_ops`,
`js_weak_job_checkpoint`, and native tracing/destruction hooks. The MVP adapter
extends the existing weak-family constructor/prototype loop. Programs that do
not use these facilities allocate no weak lifetime state.

### P2/P5: Script declarations through the retained interpreter

`$262.evalScript` now selects a Script role on the existing hosted AST executor
(**D8.1.3v24**). Its GC-owned lexical environments persist across calls, retain
TDZ/const rules and expose live bindings to subsequent interpreted and MVP
code. Ordinary indirect eval still has private lexical declarations. Script
var/function creation uses the required global-property attributes and checks
all declarations before publishing the new lexical environment.

The shared `js_interp_host_global_lexical` helper and MVP's
`mvp_lmd_dynamic_lexical` adapter reuse existing lexical-slot ownership.
`mvp_lmd_eval_script` is the host entry. Global-object binding resolution now
uses the declaring environment, including references from later Scripts and
closures. Annex B companions remain var bindings; their block publishes the
function value without rewriting existing property attributes.

### P5: atomic storage operations

MVP's Atomics namespace uses the shared built-in catalog for method names,
arity and descriptors. Its read/modify/write operations reuse the existing
sequentially consistent native atomic kernel with `JsBufferViewOps` supplying
frontend coercion/errors (**D1.3v3**). The new
`js_atomics_operation_with_ops`, `js_atomics_is_lock_free_with_ops` and
`js_atomics_pause_with_ops` entries are shared adapters, not a second typed
array representation. Programs with no observable Atomics namespace retain
their existing compiler path.

The kernel roots arguments and adopts converted scalar results before later
callbacks. It snapshots the initial length for index validation, then
revalidates bounds and acquires the current buffer address after all
coercions; a callback may detach, shrink or replace storage (**D5.2–D5.3**;
ECMA-262 §25.4.3.4, RevalidateAtomicAccess). Atomic writes also enter the
existing storage-write preparation path. Huge `isLockFree` arguments are
compared without an overflowing native integer cast. These fixes apply to
both frontends. The existing LJS synchronous-agent adjustments remain confined
to its legacy entry and are not used by the MVP adapter.

### P5: distinct hosted realms

`$262.createRealm` now retains a separate intrinsic/global owner in the existing
MVP execution. Runtime slots use the execution's existing precise roots;
callee entry, generator/async suspension and Promise jobs restore their owning
realm (**D5.2–D5.3**, **D5.4.2–D5.4.4**). Internal native brands share canonical
private names within the execution; guest class-private names remain distinct.
Eval and Function-family bodies still use the shared AST interpreter
(**D8.1.3v24**).

`mvp_lmd_function_realm` unwraps bound and proxy targets;
`mvp_lmd_constructor_prototype` reuses intrinsic slots for the
GetPrototypeFromConstructor fallback. Array species ignores a foreign intrinsic
Array before reading `@@species`. The existing RegExp brand reader is shared
with object stringification. `mvp_lmd_array_adopt_realm` preserves the realm of
arrays returned by Lambda-backed Object/Map/string operations; the optional
Promise array factory applies the same rule to combinator results. Ordinary
root-realm arrays retain the existing implicit-prototype representation.

Private pin
`61147f6dafe7b5018873d6b37ef41176967741c511e7875b3bbd1a93476fc92b`
passes **690/690** units and **23/23** focused checks with forced collection and
freed-memory poisoning. An earlier realm pin passes all **13/13** frozen
canonical hosted-realm cases. These are selected results, not a complete
current-build recount. Cross-realm subclass admission now retains Lambda
nominal ancestry for parents owned by the same execution
(**D3.4.3v5–D3.4.8**); its latest validation is recorded separately below.

### P5: waiters and deadlines

The shared `js_atomics_wait_with_ops` and
`js_atomics_notify_with_ops` entries reuse typed-array validation, numeric
conversion, waiter records, Promise settlement and the existing event loop
(**D1.3v3**). This path uses actual monotonic deadlines and matches the backing
store plus **byte offset**, including aliases with different element widths.
Waiter-owned precise roots retain storage and timer handles; settlement clears
those roots and cancels the timer. Reusing a completed native row must preserve
FIFO waiter order (**D5.2–D5.3**).

MVP constructs the public result with its ordinary object shape and the active
realm's Promise prototype. The legacy LJS agent simulation remains separate;
its synthetic values and report rewrites are not used by these entries.
The wait adapter also handles synchronous `Atomics.wait`: it reuses argument
validation/coercion and waiter registration, checks CanBlock before returning
a value-mismatch result, and waits against the actual monotonic deadline.
The calling agent's timers and Promise jobs remain blocked. Finite fractional
timeouts cannot return early. Cross-agent notification still requires the
independent scheduler below; `$262.agent` remains explicitly unsupported.

The completed asynchronous-waiter pin
`2b045f9abc3bc85a4f8ac57e307ccbf5864759a5e71ae3c66d9937721f56d43b`
passes **696/696** units, **29/29** focused forced/poisoned-GC checks,
**337/449** selected canonical and **312/424** additional strict cases.
Its 112 remaining selected failures involve blocking waits or agents.
Same-pin LJS controls pass **449/449**, and the standard stored-body LJS
baseline passes **40,261/40,261** with no retries or regressions.
Synchronous-wait validation is recorded separately on the later pin.

### P0/P2: complete intermediate recount and readonly globals

The immutable intermediate pin
`a0d493c7b25d870a3b1f5a7236cece7776e434ebb71d2292040dcf2b497df2bd`
completed all **40,261 canonical** and **37,098 additional strict** records.
Missing batch records were executed individually with unchanged deadlines.
The qualified result is **39,266/40,261 (97.53%)**; **995** IDs remain failed
or unstable. Nine more IDs pass all required modes after isolated replay but
remain excluded from that qualified count because of observed crashes/timeouts.
Raw records and qualification are preserved in `recount.json` and
`recount-qualified.json` under this continuation's capture directory.

This intermediate residual contains **310 negative phase/type mismatches**,
**173 Atomics cases**, **71 Unicode-property cases**, and **441 other
failed/unstable IDs**. The negative cases include async/generator rest-parameter
and yield restrictions, class heritage and identifier grammar. These need
shared early-error repairs; runtime rejection cannot satisfy a parse-negative
contract. The later selected Atomics results below supersede only their own
cohort, without changing this whole-audit inventory.

Against archived pin38, the qualified comparison recovers **1,830** IDs and
loses **nine**. Six losses are deterministic readonly-global assignment
crashes. Compiler type-flow analysis dereferenced a lexical declaration record
for intrinsic constants, which have no such record. The fix distinguishes
intrinsics before applying lexical and integer-loop write rules. Existing
sloppy-ignore/strict-TypeError execution semantics remain authoritative;
assignment RHS effects and shadowed local bindings are covered by unit tests.
The other three losses involve observed instability and require separate
latest-pin evidence. Repair pin
`9da9a529184c307efe35ed2a78e72812ba1a6b33eabcbc5d052f257ad6c8e1b9`
passes all **nine canonical** and **three required strict** loss replays.
Selected recovery checks do not rewrite this audit.

### Current source validation

Release pin
`129175648dc615d090badf7872ad802ade1e6bacc4d28ec5373e78b0b9e8031f`
includes the readonly-global and synchronous-wait changes. All **2,941**
production input hashes stayed fixed during the build. It passes **699/699**
MVP units and **32/32** focused checks with forced collection and freed-memory
poisoning. Selected coverage is **370/449 canonical**, **345/424 additional
strict**: **33 gains and zero lost passes in each mode** versus the preceding
asynchronous-waiter pin. All remaining **79** cases stop at unsupported agent
operations; no selected crashes, timeouts or missing records remain.
Same-pin LJS controls pass **449/449**, and its stored-body baseline passes
**40,261/40,261**, with no retries or regressions. The exception-catalog gate
passes. These are correctness checks, not benchmark timing or a latest-source
whole-population score.

The integrated `make test-lambda-baseline` gate is **not green**. Its runner
reports **7,234/7,236** including the **2,112/2,112** input gate, but represents
the unfinished GC suite as one timeout record. All JavaScript groups pass,
including **699/699** MVP cases. The two failures are:

- `BinarySchemaTest.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`:
  `test/pdf_output_check.py` cannot import Pillow (`ModuleNotFoundError: PIL`).
- `test_mir_gc_stress_gtest`: **298 cases complete** before the existing
  360-second idle deadline stops `regression_music_layout` in `interp-forced`.
  That case's forced-JIT and randomized-GC modes completed successfully. This
  timeout is unresolved; it is not a passing stress gate or an established
  regression attribution. No deadline or expected result was changed.

The current aggregate count therefore cannot be compared as a complete
population with earlier baseline totals. Exact logs/JSON are preserved in
`blocking1/gate-artifacts/`, with hashes and `blocking1-closeout.json` beside
the other captures. All 2,941 production input hashes still match the pin.

### Outstanding structural work

Real agent scheduling and cross-agent blocking-wait wakeup remain open. Lambda's precise-rooted
Activation stacks are the suspension seam, but each agent also needs its own
job queue, kept-alive weak targets and runtime owner. Hosted realms currently
share one agent and must not be repurposed as independent agents. Broadcast
must share backing storage without sharing ordinary object identities.
SharedArrayBuffer growth and waiter/job teardown need integrated agent tests.
Child-realm dynamic imports also remain explicitly unsupported until they own
a module graph.

The agent implementation must separate runtime-state creation/destruction
from whole-EvalContext capsule teardown, preserving agent ownership across
Activation and timer callbacks. SharedArrayBuffer broadcasts need distinct
realm-local carriers retaining the same backing handle, including growth;
copying the handle would break identity, while sharing its pointer without an
owner would double-free it. Blocking waiter records must remain owned until
their suspended consumer resumes. Interpreter loop/Atomics scheduling points
must let actual agents progress without fabricating loads or reports. These
are unfinished implementation requirements, not features exposed by this pin.

The current Unicode 16 tables are unchanged. Choosing the frozen Unicode 16
contract or upgrading the shared runtime and target to Unicode 17 remains a
user decision. The complete recount above predates the realm, waiter and
readonly-global increments. Its result must not
be presented as a current-build score or combined with selected later gains.
Canonical sources, expected outcomes and deadlines are unchanged. Performance
has not been measured in this continuation; earlier regressions remain open.

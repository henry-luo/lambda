# Lambda MIR MVP: language parity and untyped execution

**Date:** 2026-11-05

**Status:** Active, resumed 2026-11-09. P44–P50 close the five PDF/EPS failures and the native husk/GC fixture failures. Current work tunes untyped release execution while preserving typed performance (§10.91–10.96). Watercolor's normal timeout and final language qualification remain open. No full compatibility or performance closure claimed.

**Starting tree:** `71296a16a9622f764229720ed2c321330efcd278`, branch `master`.

Extend the existing compiler in `lambda/runtime/mvp/` to execute the full
Lambda language surface, pass the Lambda GTests, and beat Full MIR on every
registered untyped benchmark. Preserve the existing typed benchmark performance.
Features that Full MIR already routes to the AST interpreter may use that same
route in MVP. This does not require a second implementation of those features.

This continues the [MVP design](../Lambda_Design_MIR_MVP.md) and
[typed implementation record](Lambda_Impl_MIR_MVP.md). The runtime value model,
object layouts, type system, scheduler and precise GC remain shared.

## Current checkpoint

- **Full MIR retirement, 2026-11-10:** the interpreter uses MVP APIs directly; Full emitter/build branches and public-return variants are removed. Current task checkpoint: [MVP interpreter alignment and retirement](Lambda_Impl_MIR_Full_Retirement.md#current-checkpoint). The release runtime archive builds, but full-host validation is blocked by Radiant event-protocol mismatch **RI-004**. Root executables remain the prior default-switch build. No retirement runtime correctness/performance qualification is claimed.

- **Historical default-compiler integration, before retirement, 2026-11-10:** MVP is the default for eager MIR and AUTO/interpreter promotion; Full remains explicit `production`. Both root executables are rebuilt in release. Final validation passes **13 focused Python methods and 9 selected Lambda GTests**. No baseline or benchmark sweep ran. Changes are uncommitted; no jobs remain running. Identity and receipts: §10.100 / `temp/mvp_default_switch/final-summary.json`. Prior P101 performance receipts below remain historical, not measurements of this integration.

- **Previous performance checkpoint, before default integration (2026-11-10).** Retained **P101**, SHA256 `d51a190813cd9847482d88d7c7e60655e5aed35eb03de6e741a57611bc99cb7f`; compiler sources matched its manifest at that checkpoint. Ten focused methods passed, plus a final paired/GC constructor check with the added recursive/mixed-kind cases. P101 Splay: **208.439/Full 207.540/P82 212.163 ms**, 31 pairs; still **0.43% slower than Full**. DeltaBlue, Pnpoly and Mbrot have P92 31-pair Full wins. P99's three focused typed repeats have no slowdown flags; P101 has no complete typed qualification. Last complete campaigns remain P81 untyped / P92 typed. P102 is discarded. Rebuild the runtime archive before linking: it was last built with P102. Next: close Splay, then current 77-case untyped/typed milestone screens. Details: §10.99 / `temp/mvp_untyped_tuning_goal/batch3-checkpoint.json`. No baseline, shared executable replacement, sibling edit or commit.

- **Resumed 2026-11-10:** new user-requested goal on clean HEAD `8aca6eeb7` (P82 tuning committed in `cc2c40dbe`). Retained compiler identity still matches P82. Continue all remaining untyped misses together and preserve typed performance. The frozen P81 eleven-crossing gate remains satisfied; avoid another aggregate campaign until the next implementation milestone. P84 investigates loss of the coarse Map carrier across private calls whose normal record results have different layouts (**D4.3.1 / D7.4.1 / D6.3**). No root executable replacement or baseline is planned.

- **Resumed:** 2026-11-09 at the user's request, master `14f48f209`; merge complete, initially clean tree.
- **Latest continuation:** master `1540d9228`. After the user updated Radiant to `558eb3e`, the original private release build/link succeeds. [RI-003](../../../radiant/vibe/Radiant_Issue_Ledger.md#ri-003) is resolved: both property providers occur exactly once in `libradiant.a`, required symbols are defined, and typed/untyped Fibonacci goldens pass under both compilers (4/4). MVP record-read tuning resumes below (§10.92); the owner boundary is unchanged (**D9.1.1v4 / D9.1.3v2**).
- **Scope:** the latest request prioritizes untyped MVP performance against Full MIR on all 77 benchmarks, with no typed MVP regression. Preserve the existing correctness changes, formal semantic oracles and shared runtime (**D4.3.1**, **D8.3.1–D8.3.3**).
- **Current evidence:** P36 passes 14 focused unit methods and all 77 typed/untyped benchmark goldens. Its complete private-release untyped campaign has 48 faster / 29 slower Full medians, versus 47 faster for P26 in the same run; geometric MVP/Full 0.67503 and MVP/P26 0.93323. Typed MVP/P26 is 1.00349 over 76 nonzero rows, with no reproduced slowdown after 17 focused 15-pair checks and independent 30-pair Array1/Sieve checks. All source/binary/corpus/runner identity checks pass. This qualifies the measured benchmark corpus, not the deferred full language/GTest gate.
- **Milestones:** reproduce the six late source cases; fix PDF/handler and EPS/container causes with permanent paired/GC cases; investigate watercolor and native/harness residuals; reconcile aggregate test-target migration and run current parity gates.
- **Verification budget (latest user instruction):** validation is deferred until at least **11** previously slower benchmarks cross below Full MIR, superseding the earlier six-case gate. Focused release performance probes retain their existing golden sanity checks so incorrect results cannot count as wins. No unit suites, aggregate typed/untyped campaign, Lambda/Radiant baseline or full-language gate before that threshold.
- **Open qualification:** `make test-lambda-baseline` currently stops because `test_wpt_html_parser_gtest` has no generated build target. Old broad counts below remain historical; do not present them as current failures.
- **P44–P47:** PDF output demand/handler rollback, boxed native range bounds and stale var-argument transport are repaired (§10.88). All five original PDF/EPS source cases pass paired normal/forced/seeded GC checks. Three permanent MVP regressions pass 9/9 GC checks and fail on the frozen pre-fix control. Full retains a newly exposed projected-borrow mismatch; its oracle remains unchanged.
- **P48–P50:** dead native wrappers receive consistent ordinal handling, and their fixture now owns precise roots, compares JS Set completion and understands receiver-specific binding variants. Seven native units pass 21/21 across GC modes. A failure-only replay passes 35/37; its two macOS fork failures pass 2/2 with GTest's fresh-process death-test mode (§10.89).
- **Watercolor:** the unchanged script matches its golden on MVP with a 300-second diagnostic budget. Its ordinary timeout still fails; debug runs establish correctness only. No release timing or threshold waiver is claimed (§10.90).
- **Release tuning:** artifacts are under `temp/mvp_untyped_tuning_20261109/`. P8's complete release screen passes all 77 untyped goldens: 35/77 faster Full medians (starting 30/77), geometric MVP/Full ratio 0.88121 (starting 1.07515). All 77 typed goldens pass, with overall candidate/starting-MVP ratio 0.99847. P9 repairs the single confirmed typed regression and is the current private release; only its affected workloads are remeasured (§10.91).
- **Next:** tune the 29 slower rows in P36's complete untyped campaign, prioritizing recursive record transport, remaining array/search kernels and text pipelines. §10.95 records every slower row; `temp/mvp_untyped_tuning_p27/p36-summary.json` retains raw screen classification and follow-ups. No every-row target is closed. Retain deferred language qualification and owner-ledger defects. Root executables remain untouched; no Lambda/Radiant baseline, C2MIR or full language verification ran.
- **Current build gate:** cleared. P36 is the current private release under `temp/mvp_untyped_tuning_p27/`, frozen by `p36-manifest.json`. P26 under `temp/mvp_untyped_tuning_p13/` supplies frozen Full/prior-MVP controls. Starting HEAD is `84f18b39b`; no commit was requested. RI-003 remains resolved; no sibling-owned change was made.
- **P10–P12:** guarded inferred-record reads and compile-time binding/call/return candidates are implemented (**D4.3.1 / D7.4.1 / D2.6.2**). Frozen P12 SHA256 `b1e56d966cbe104fb27484f761608456ca49e7eebb3ed39fea2337ff5582771d`. All 77 untyped goldens pass; geometric candidate/prior-MVP ratio 0.95626 and MVP/Full ratio 0.84347, with 35 faster / 42 slower Full medians. Focused units have no MVP failures; Full retains the known three LR07-53 subtests. All 77 typed goldens also pass, with geometric candidate/prior-MVP ratio 0.99844 over 76 nonzero rows and no confirmed slowdown after 15-pair follow-ups (§10.92), under `temp/mvp_untyped_tuning_next/`.

- **P13–P22:** clean starting master `d3582e267`, frozen P12 Full/prior-MVP control. Scalar body versions guard constructor predictions, retain the original ABI/effects and widen all local writes (**D7.4.1 / D4.3.1 / D6.3**). Open implicit record writes use ordinary COW; inferred integer reads/writes guard live descriptors. Boxed array witnesses reuse the shared physical-storage guard, after numeric alternatives. P21 excludes member/index writes from scalar narrowing; P22 clears the boxed witness when captured homes reload. Eleven final focused unit methods pass, including both compilers and normal/forced/seeded GC cases. Frozen P22 SHA256 `ec19808d13a2915d322dd5741edf9c24609ce6aef71fc9f4425fcebb2ec9a833`. All 77 untyped goldens pass: 47 faster / 30 slower Full medians (prior MVP 35 faster in the same campaign), geometric MVP/Full ratio 0.72208 and MVP/prior ratio 0.85538. All 77 typed goldens also pass; follow-ups identify the Splay regression addressed in P26 under `temp/mvp_untyped_tuning_p13/`. Interrupted P18/P20/P21 screens are partial diagnostic evidence, not final campaigns; no every-row closure is claimed.
- **P23–P26 complete slice:** diagnostics identify a literal-shape hint carried past declared return admission. P26 honors concrete declared map results before constructor hints (**D4.2.5 / D7.4.1 / D4.3.1**); generic `map`/`object` results retain ordinary prediction. Eleven focused unit methods pass, including the typed-result COW/MIR regression case. Frozen P26 SHA256 `39edeb116459fadca1ed71b97461c96a86ef4714a8ea331bdb4227d8d0d19ef7`. All 77 untyped goldens pass: 48 faster / 29 slower Full medians, versus 37 faster for prior MVP in the same paired campaign; geometric MVP/Full 0.71611 and MVP/prior 0.84814. All 77 typed goldens pass, geometric MVP/prior 0.99861 over 76 nonzero rows, with no reproduced slowdown after 15 focused 15-pair checks and an independent 30-pair Primes check. All provenance checks pass in `temp/mvp_untyped_tuning_p13/p26-summary.json`. The every-row Full target remains open.
- **P27–P36 complete tuning slice:** resumed from clean `84f18b39b`; starting compiler sources match frozen P26. Artifacts: `temp/mvp_untyped_tuning_p27/`. P36 preserves audited inferred-array seeds, queries immutable initializer/closed-call intervals before range publication, guards boxed integer/Error locals through the finite numeric-loop proof, and supports integer candidates in guarded scalar bodies (**D4.3.1 / D6.3 / D7.4.1 / D8.2.5**). A numeric consumer can predict a version when opaque callers supply no scalar hint; exact entry guards retain other domains. Native loop carriers use independent registers and publish once on exit or before checked resumption; entry misses publish nothing, and loop-local declarations are excluded from entry narrowing. Inferred arrays retain ordinary constructors. Boxed array/Error owners reuse the shared physical guards for native iterations and strided loops. P33's partial screen exposed the lost Primes store version; P35 repairs it (2.648 ms versus P26 MVP 5.068 ms and Full 11.996 ms). Puzzle and Collatz gains remain. Fourteen focused unit methods pass on frozen P36 (SHA256 `c417537683b6ec9d768d1133fddefc1678098f49bbd072bae6f846f6040511e6`); both complete screens pass all 77 goldens. Seven untyped and 17 typed 15-pair follow-ups have no statistical slowdown flags. Independent typed 30-pair Array1/Sieve checks also have none; their emitted MIR matches P26 after address normalization. Two new fixture/golden pairs and the guarded-scalar fixture cover snapshots/open writes, finite-loop entry/resumption/exit, opaque integer/float/null/invalid/wide inputs and native boxed-array stores. Unconditional bitwise-int inference was discarded before qualification because boxed poison retains existing Error behavior. Full's nonrecursive open-var replacement mismatch is reproduced on P26 and recorded under LR07-53; its oracle is unchanged. No baseline/full-language gate ran.

- **Active goal continuation:** retain the uncommitted P36 slice on HEAD `84f18b39b`; frozen P36 is the prior-MVP control and P26 supplies Full. Artifacts: `temp/mvp_untyped_tuning_goal/`. Validation began only after P46 confirmed **12 crossings** (`p46-validation-unlock.json`). Last aggregate-qualified private release is **P50**, SHA256 `b6ae963fe5db8f4eb76038e0726dc77e49c8d1e642eceb3457f091939dd2628d`. Its complete untyped screen passes all 77 goldens: **61 faster / 16 slower Full medians**, geometric MVP/Full **0.60937**, MVP/P36 **0.89681**. All nine source/binary/corpus/runner/release checks pass in `p50-summary.json`. P50 passes six focused unit methods after P47's 17; both new fixture/golden pairs pass paired normal/forced/seeded GC. P48 supplies the complete 77-case typed screen (geometric MVP/P36 **0.99010**), with all flags cleared by focused follow-ups; the corrected P50 Deriv (30 pairs), Richards wrappers and Raytrace3D (15 each) have no statistical slowdown. The aggregate typed screen is P48, not P50; P50 qualifies changed inlining paths with focused follow-ups. No jobs remain running. Next: tune the 16 remaining rows together, preserving typed performance and the 11-crossing validation cadence. Shared next-root candidates are recorded at the end of §10.96. No baseline, sibling edit or commit ran; the goal remains active.

- **P51 onward active batch:** validation waited for **11 confirmed crossings on frozen P81**. Last complete P81 untyped screen: 77/77 goldens, **72/77** faster Full medians (68 bootstrap wins), geometric MVP/Full 0.5332. Complete typed screen: 77/77 goldens; its FFT/Brainfuck regression flags are fixed and resolved by **retained P82** focused repeats (31 pairs). Sumfp remains below timer resolution. Current focused Full misses: Pnpoly, Mbrot, Splay, JetStream DeltaBlue; Nqueens/Paraffins have 31-pair wins. Small untyped CD/Parse Integers prior-MVP regressions remain. P83 Boolean-branch experiment is discarded; compiler sources again match P82 SHA/manifest. Focused semantic/GC/MIR checks pass, and Full defects LR07-54/LR07-55 are recorded with unchanged MVP goldens. **No jobs running; work stopped at the user’s wrap-up request (2026-11-10).** No P82 aggregate, baseline or commit is claimed. §10.98 and `temp/mvp_untyped_tuning_goal/p82-summary.json` hold the resume state.

### Previous checkpoint (2026-11-06)

- **Updated:** 2026-11-06. Goal paused at the user's wrap-up request: finish the known language gaps, then
  qualify Lambda/GTests and benchmark performance. HEAD `a9b813686`, `master`;
  P16 onward remains uncommitted. No commit was requested.
- **Verification policy:** focused unit tests only while implementing features,
  per the user. No intermediate Lambda baseline/full verification, Radiant
  baseline or C2MIR execution. Root executables remain untouched.
- **Prior full evidence:** P15 `p15_ndim_owners`, SHA256
  `b81fef5ba18aa42050cadaa25a6c556b46d60555adab86cf3f73da0933fc80f0`.
  Its 177 focused checks, 970 Python CLI checks, typed/untyped 77 each, selected
  native checks and 137/152 real-harness result qualify that prior tree only
  (§10.59). Current evidence does not establish final correctness/performance.
- **P16–P18 completed slices:** canonical N-D values and rank-aware length;
  named declaration snapshots; file/PDF constructor outcomes; native views,
  state, events, edits and cold callback ownership; scalar/projected var homes,
  once-evaluated place keys and wide-field lanes (§10.60–10.62). Their focused
  script/native results remain recorded with each frozen milestone engine.
- **P19–P22 completed slices:** ordered dependent defaults with no default
  selection (S11.4.8v2/TG18); native handler task-scope analysis; traversal key
  cleanup and query transfers; safe range proofs; shared stable native join
  streams, optional rows, indices and grouped tuples (S14.1.1v2/S14.1.3).
  Results: 57 binder script checks, normal/GC dispatch unit, 9 traversal checks,
  30 paired join checks and 33 permanent query/traversal checks (§10.63–10.66).
- **P23 implemented:** shared lexical type-use metadata survives scope rebinding
  and aliases. Captures compare declaration identity, including same-spelling
  binders. Dependent admission carries exact rooted captured selections beside
  local slots, without TLS or TypeBoundRef layout/back-pointer changes
  (D4.1.4/D8.2.4). Array element access preserves binder identity; structural
  record checks reuse the env-aware field proof. Return checking defers an
  unknown `T:type` payload domain (§10.68).
- **P23 focused evidence:** `p23-captured-final-unit.json` passes **6/6** on the
  current two fixtures across GC modes, including defaults, return-only uses,
  arrays/maps, cold floats, Error payloads, shadowing and nested site selection.
  `p23-binder-python-unit.log` passes **60** selected checks, including **54**
  existing paired binder checks. Full controls retain their failures; no oracle
  was weakened. Earlier failed attempts are retained.
- **P24 implemented:** local type method syntax uses callable depth relative
  to its declaration. Methods were previously discarded inside functions.
  Local/saved/ordinary receivers pass **18/18 paired GC checks** in
  `p24-local-method-unit.json`; the permanent method unit includes the fixture.
- **P25 implemented:** methods retain per-declaration lexical environments,
  inherited methods and escaped constructors. Bound lexical methods own their
  receiver and source Function; COW copies retain attachments. Shared closure
  flow follows type/instance retention, rejecting escaped capture writes.
  Dynamic dot/index pn calls prepare the entire writable receiver place and
  evaluate keys once (S9.1.3v3/S9.1.4v2/S12.3.3v2). Nominal aliases preserve
  the complete descriptor instead of truncating TypeObject to TypeMap.
- **P25 storage:** the existing native collection storage/hash/order/ephemeron
  algorithms moved from JS into `native_collection.cpp/.hpp`. Lambda and JS
  share one implementation; CLI-only builds need no JS realm or stubs for
  this storage. Public Item/container/type layouts remain unchanged (D6.1.1).
- **P25 focused evidence:** new capture/default/annotation/recursion/COW cases
  pass 9/9 GC checks; existing methods pass 30/30 paired checks; nominal aliases
  pass 9/9 across MVP, Full and T0. The escaping-write negative passes all three
  tiers. Selected permanent Python unit passes 12 CLI checks. Shared JS storage
  passes 9/9 script checks; the native no-realm ephemeron unit passes normally
  and with GC poison, including value-to-key cycles (§10.70).
- **P26 implemented:** nested type assembly owns a scoped resolver state, so
  closing an inner declaration preserves outer fields, layouts and methods.
  The new nested fixture passes 9/9 across MVP/Full/T0 and GC modes; lexical
  capture and writable-place controls pass 12/12 (§10.71).
- **Current qualified focused release:** `p34_retained_t0_completion`, SHA256
  `44b359ee1fa63452daf0b9081d135f00da99e1a2c9b82d2949b2049130c91347`.
  Its 3,545 source freeze includes the corrected native ephemeron unit. Use `release-commands-p25-native.json` and
  `release-input-extension-p25-native.json` for subsequent frozen builds: they
  include the new runtime translation unit. The object reuse chain includes
  a recorded failed build with valid individual objects; no failed executable
  is used. Root executables remain untouched.
- **P27 implemented:** normal error-union admission converts before boxing;
  inlined var input/publication use actual success lanes and Item outcomes;
  implicit body results participate in effect planning. Constructor source
  stamps and boundary labels share emitters with Full. All six prior failures
  pass; 66 selected CLI unit checks pass across normal/GC modes, including
  Full controls, scalar places and captured dependent annotations (§10.73).
- **P28 implemented:** pattern/range members retain Item transport while type
  values keep their independent carrier. Scalar matches, nullable equality,
  element COW, record reification, normal float stores and boxed numeric
  dispatch retain their actual domains/effects. The frontend keeps all
  comma-separated aliases in the semantic tree. The permanent focused unit
  passes 90 CLI checks, including paired original controls and new MVP/T0
  checks (§10.74). Full's separate deferred float-store defect remains visible.
- **P29 implemented:** checked write-place admission recognizes nullable
  array success domains; functional braced loops honor their enclosing result
  demand. The permanent result/place and traversal units pass 15 CLI checks
  across normal/forced/seeded GC. Full's existing traversal-continue failure
  remains recorded (§10.75).
- **P30 implemented:** handler inference joins the actual recovery domain
  with the surviving operand domain; raw C pointer results preserve zero as
  absence before boxing. The three selected recovery/name/symbol fixtures
  pass 18 paired CLI checks across GC modes (§10.76).
- **P31 implemented:** all six borrowed-outcome/task failures are fixed.
  Insertion capture follows the shared ownership predicate through conditional
  values. Array membership cannot skip value-dependent admission. Split keeps
  its Error arm until propagation. Captured activations retain access facts;
  coordinate writes reuse the checked COW path, including projected receivers
  and once-evaluated keys. Full/MVP share rooted select and coordinate staging.
  The permanent unit passes 57 CLI checks across GC modes (§10.77).
  `p31_shared_coordinate_places` is the exercised binary; the current release
  restores its exact argument-limit policy and changes the select comment only.
- **P32 implemented:** Full/MVP share lazy admission source labels; parameter
  rejection reports the call's source and declaration outcomes report their
  declaration. A pure function named main remains an ordinary callable.
  Identical inferred record layouts need no reification, and inferred binding
  storage performs carrier conversion without creating a user contract.
  The selected permanent units pass 123 CLI checks across GC modes (§10.78).
- **P33 implemented:** virtual-array length uses the checked generic producer;
  file/conditional results retain their Error alternatives, and unresolved
  native namespace values publish the shared Error sentinel. All 13 original
  MVP failures pass. The selected permanent units pass 201 CLI checks across
  both compilers and three GC modes (§10.79).
- **P34 implemented:** retained interpreted Functions can be called without
  an active interpreter runner, reusing the shared retained-callback state.
  Escaped faults release it before forwarding; callee-local handlers keep
  their fault boundary. The native MVP → T0 → MVP callback, view/edit/event
  units pass 12 checks across GC modes, including outer transaction cleanup,
  cold floats, Error and container retention (§10.80).
- **Historical census:** P12 recorded 320 MVP-only failures, 219 shared,
  17 Full-only and 250 unpaired. P16–P33 resolve the selected root-cause
  families; that old population is not a current gate. Spread does not expand
  call arguments under S12.3.5v2.
- **Feature inventory review:** §10.3 replaces the stale P0/P1 statuses with
  the implemented routes and focused evidence. No known public AST-family
  lowering remains unimplemented within the existing Full/T0 surface.
- **Final gates starting (P35):** freeze the current engine/harness/corpus,
  then run all applicable Lambda GTests and the aggregate Lambda baseline,
  typed/untyped correctness and timing/no-regression qualification. Failures
  remain open and resume focused root-cause work; no final pass is claimed.
- **P35 actual gate progress:** frozen release `p35_language_feature_closure`,
  SHA256 `8992dd73a74fd7b6f1971161448405ba0fdca3ca13b2beb53b14daaef59058c6`.
  The original paired run finished its 112 compiler chunks: 2,153/2,233 cases
  have paired reports; chunks 30 and 32 lack MVP reports after timeouts.
  MVP passes 1,698 and Full 1,824 on that frozen old engine. The 304 shared,
  151 MVP-only and 25 Full-only failures are historical residual inputs,
  not current-engine counts (`p35-full-language-gate-summary.json`). Native results: security
  98/98; errors 307/311; concurrency 93 cases with 48 failures and a failed
  suite setup. Context-switch failures cascade in the concurrency process;
  isolate them before attributing all 48 to MVP. No baseline or timing ran.
- **P35 Python result:** 103 semantic/emission methods, 1,985 recorded CLI
  invocations, 80 nonzero exits and 100 failed assertions/subtests. Typed
  goldens pass 72/77 on MVP and 77/77 on Full. Failures include stale shared
  oracles/emission expectations as well as genuine MVP rejections; preserve
  them until classified (`p35-python-full-gate.log` and invocation manifest).
- **P36 partial fix:** homogeneous literal joins preserve their scalar
  carrier without introducing inferred refinements. Conditional results keep
  the actual condition Error alternative; procedural protected operands keep
  their producer's successful domain. All three graph import failures,
  `proc/handler_results` and a public conditional-string probe now pass.
  Candidate `p36_join_carrier_domains` passes 18/28 selected paired checks;
  it does not close the remaining array/fill/binder/closed-region failures.
  The open-parameter admission patch preserves already-proven actual domains;
  array constructor outcomes, generic match results, raise effects and closed
  local carriers have also been repaired. `p36_closed_admission_domains`
  passes 24/24 selected paired checks after the CD and Three-way Merge fixes.
  Five focused GC/admission methods also pass (§10.82).
- **P37:** Error-only admission uses status rather than treating its successful
  Error tag as rejection (S11.4.2v3). Counted arrays recheck lengths after
  resizing (D4.3.3), and arbitrary callable-pipe targets retain application
  checks (S10.1.7v2). Six focused Python units pass; all three selected native
  negative tests pass on MVP. Full fails one diagnostic unit (§10.83).
- **P38:** optional Bool call results restore their scalar category
  before boxing. Array-domain and modular-update fixtures pass on both
  compilers. A grammar declaration with no value initializer entered inferred
  value widening; repeated nullable descriptors prevented convergence. The
  type-declaration/idempotence patch passes grammar-lint and selected package
  units, including forced/seeded GC (§10.84).
- **P39:** imported Error-only returns use `none` for their successful domain
  (S11.1.7), fixing a mail-import compiler crash. Two GC units pass 48 CLI
  checks. Targeted replays pass 117/118 and 16/21 prior MVP-only source units.
  No complete-population pass is claimed (§10.85).
- **P40–P43:** statement result/colour checks, local alias mutation proofs,
  collapsed procedural-loop demand and admitted pointer-union transport close
  the six late procedure/scene cases. The final three original failures pass
  6/6 paired checks; the permanent affected unit passes 36/36 CLI checks across
  normal/forced/seeded GC (§10.87). Temporary runtime origin logs are removed.
- **Current next work:** six late cases remain: PDF output streaming, EPS
  fonts/text/machine/raster and the watercolor timeout. `pnpoly2` passes paired
  standalone goldens but its prior harness failure needs attribution. Two
  unreported chunks, native concurrency residuals and old Python assertions
  also need focused classification. No new full gate or baseline has run.

## 1. Acceptance contract

The expansion has three independent completion gates. A performance aggregate
or an interpreter-only test run cannot substitute for any gate.

| Gate | Required result |
|---|---|
| Language and GTests | Every currently implemented public Lambda feature has a recorded execution route and semantic coverage. The complete applicable Lambda GTest population passes with MVP selected for JIT execution. Positive, negative and error-payload cases all retain their existing oracles. |
| Untyped benchmarks | Every registered canonical untyped entry runs correctly in MVP. For every valid Full comparison, the one-sided 95% paired-bootstrap upper bound of MVP/Full execution time is strictly below `1.0`. Slower, tied, invalid and inconclusive rows remain open. |
| Existing typed benchmarks | All typed outputs and ownership checks pass. No new statistically supported regression against a freshly captured starting MVP; investigate slower rows and retain unresolved results. Close the existing CD/Base64/DeltaBlue qualification debt before claiming complete performance closure. |

P0 freezes the populations, including discovered tests and excluded slow
benchmarks. The current benchmark registry contains 77 entries across eight
suites. If the registry changes, append the new entries explicitly and record
the population change. Do not quietly remove difficult rows.

"All language features" includes modules, packages, objects, document values,
views, resources and concurrency, as well as scalar expressions. Full MIR
parity is the minimum coverage reference, not the semantic oracle. A feature
that both engines implement incorrectly remains a defect under the formal
specification. Features explicitly marked unimplemented in the specifications
must remain visible in the inventory; do not label them supported merely
because Full MIR also lacks them. This plan does not invent syntax or settle
open semantic rulings.

The default AUTO policy and persistent T0 REPL remain as specified by
**D8.3.1–D8.3.6**. Switching the default compiler, replacing Full MIR, and adding
MVP background promotion are separate rollout work, not prerequisites for an
explicit MVP JIT lane.

## 2. Authority and implementation constraints

| Concern | Governing rulings and consequence |
|---|---|
| Representation | **D2.3.1–D2.3.5**: separate semantic type, value representation, ABI and register class; preserve `Item` and the canonical conversion router. |
| Untyped inference | **D4.3.1–D4.3.4**: boxed execution is the semantic reference; body inference does not create a source contract; inferred array narrowing belongs to its binding. |
| Calls | **D7.2.1–D7.2.4, D7.3.1–D7.3.5, D7.4.1–D7.4.3**: definition-site identity, boxed closures, complete guarded variant keys, explicit context/operands and completion transport. |
| Analysis | **D8.2.4–D8.2.6**: use the shared AST index and pass manager; semantic facts stay on AST analysis; demand lowering must preserve behavior. |
| Ownership | **D5.4.1–D5.4.6, D6.2, D6.3**: preserve COW snapshots, caller-owned `var` homes, scalar homes, precise root slots and recovery boundaries. |
| Collections and control | **S2.5, S2.6, S12.1.1v3–S12.1.4v4, D3.3.1**: retain functional collection, procedural last-result behavior, effect color and destination-specific append. |
| Errors and contracts | **S7.4–S7.11, S11.4.1v5, S12.3**: retain Error identity, evaluation/short-circuit order, rich admission failures and call validation. |
| Objects and queries | **S11.2–S11.3, S14.1–S14.3**: matching, structural versus nominal membership, joins, ordering and windows retain their declared semantics. |
| Tasks and resources | **S12.4, S13, D7.5, D6.3.6**: reuse the scheduler, activation and resource ownership rules across native/interpreted calls and suspension. |
| Verification | **D8.6.1–D8.6.3**: retain emission ratchets, release MIR dumps and forced-GC/poison oracles. |

No formal ruling change is proposed. If implementation exposes an unresolved
semantic question, record the exact ruling gap and ask before dependent work.

Use existing runtime kernels and shared physical emitter primitives. Before
adding a helper, find its current owner. Promote or parameterize a shared
implementation rather than copy production AST lowering or build a parallel
semantic library. Any helper change must identify its GC, reentry, argument
ownership, result-home and completion contracts.

Retain the removed C-text backend's removal; C2MIR is not an acceptance target
for this expansion. Do not edit vendor code, restore native-stack GC scanning,
add annotations to make an untyped benchmark easier to compile. Canonical
workloads stay fixed within a measurement corpus. The user's later approval
allows repairing stale ports/oracles, with the original corpus retained and a
new source freeze before any comparison (§10.7).

## 3. Starting gaps found in the source

These observations identify work; they are not a completed coverage census.

| Current boundary | Evidence and required expansion |
|---|---|
| Compiler selection | `runner.cpp::load_script` requires explicit JIT for MVP and rejects MVP with AUTO/interp. The GTests deliberately invoke all three tiers. A global environment override is therefore insufficient. |
| Function planning | `mvp_plan.cpp::lambda_mvp_plan` rejects captured, variadic, dependent and suspending definitions, and most nested named definitions. Rejection diagnostics are not always precise descriptions of the predicates. |
| Calls | `admitted_call` rejects named/spread/pipe arguments and indirect calls. Existing support includes direct fixed-arity calls, some defaults, public imports and noncapturing function values; extend it rather than replace it. |
| Content and control | Planning rejects multi-value functional content and non-while loop forms. Query support already includes grouping/ordering, but multiple sources, optional/keyed forms, `let`, limits and offsets remain restricted. |
| Patterns and recovery | Match admission is limited to base-type cases; filtered handlers and further contract forms remain outside the admitted subset. |
| Remaining syntax | The admission switch rejects node families outside its list. Inventory elements/lists, decomposition, objects, navigation, document operations, views and tasks by actual AST kinds and tests. |
| Builtins | Many fixed pure registry calls work; variadic, mutating and suspending cases need their real ABI/place handling. Simply deleting the builtin allowlist is unsafe. |
| Untyped measurement | `lambda_mir_mvp.py` selects `<name>2.ls` in its execution loop. All 77 `<name>.ls` files exist, but coverage, output validity and performance have not been qualified for MVP. |

The previous broader MVP gate reported 62 failure headers, including emission
assertions, semantic/GC failures and four Full-only negative-probe failures;
two historical reference failures concern C benchmark controls. These are not
62 independent MVP language features. P0 must reproduce and classify the
failures before choosing fixes. No known semantic failure is grandfathered into
the language-parity exit gate.

## 4. Execution model and delegation

### 4.1 Three visible routes

Record one route for each definition/feature using the shared planning facts:

1. **MVP native MIR:** specialized scalar/container/call lowering with proofs.
2. **MVP boxed MIR:** complete generic lowering through shared semantic kernels.
   This is the correctness fallback when specialization cannot prove its shape.
3. **Existing interpreter delegation:** only where the audited Full execution
   path already delegates that feature. Preserve its boundary and runtime ABI.

Routes 1 and 2 belong to MVP. Route 3 counts as supported language behavior,
but must be reported as delegated. A runtime builtin call is not automatically
interpreter delegation. MIR's bytecode interpreter, used by code-generation
policy/diagnostics, is also distinct from the Lambda AST interpreter.

P0 must build the actual delegation allowlist from current source and execution
traces. Do not assume closures, async procedures, views or object methods all
delegate: several have real generated lowering in Full MIR. The shared dynamic
dispatcher already supports interpreter-owned `Function` values through
`lambda_dynamic_invoke_by_count`, `interp_call` and `interp_call_borrowed`.
Reuse those routes where applicable, including the promotion policy already
owned by the runtime. Do not invent a parallel dispatcher.

Trace subsequent promotion as well as the initial call. A delegated function
must not quietly promote through Full MIR under an MVP label. A published
image must honor the selected compiler; a body MVP cannot promote retains its
permitted T0 route under **D8.3.3**. Any remaining production-compiled execution
is reported explicitly and cannot qualify an MVP benchmark win.

For a feature Full MIR compiles, a temporary missing MVP lowering remains an
open item. An unrestricted catch-all interpreter or production-compiler
fallback would conceal the gap and does not satisfy this plan.

### 4.2 Boundary invariants

- Evaluate operands/arguments once, in source order. A guard miss consumes the
  already evaluated values; it must not repeat side effects or initializers.
- Keep module identity, imported bindings, constant pools and retained AST
  lifetimes valid through callbacks and any delegated execution.
- Use caller-owned homes for `var` transport and explicit scalar-return homes;
  never let a borrowed wide scalar outlive its owner.
- Treat unaudited helpers and interpreted/dynamic calls as collecting and
  reentering. Reload movable data buffers and invalidate affected shape/place
  facts after such boundaries.
- Preserve closure snapshot/live-borrow permissions from the shared analysis.
  A closure stays boxed; an escaping environment is traced precisely.
- Use the existing activation/scheduler for suspension. Do not reconstruct a
  suspended body by replay, create a second coroutine engine, or jump between
  native stacks to transport an ordinary Error.
- Preserve pure/procedural checks, resource cleanup and error payloads across
  native-to-interpreted and interpreted-to-native calls.

## 5. Implementation phases

Dependencies: **P0 → P1 → P2 → P3 → P6** for language closure, and
**P0 → P1 → P2 → P4 → P5 → P6** for untyped speed. Start small untyped
measurements after P1/P2; finish remaining language families without waiting for
every benchmark to be tuned. Each phase has a finite exit list.

### P0 — Reproducible coverage and measurement

**Changes**

1. Freeze the real GTest discovery list, negative-test rules, platform goldens,
   slow-benchmark exclusions and per-suite execution modes. List registered
   Lambda semantic tests separately from Full-specific emission tests and
   independent C reference checks.
2. Add compiler-aware execution to the shared test harness. Ordinary semantic
   scripts get a forced-JIT MVP lane. Tests that explicitly request `interp`
   or `auto` retain those modes without the incompatible MVP override; their
   JIT counterpart uses MVP. Preserve existing tier-parity assertions and
   specialized environment settings. In-process compiler tests must also
   exercise the selected backend, not only CLI subprocesses.
3. Register an aggregate MVP semantic gate as `make test-lambda-mvp`, reusing
   the population in `build_lambda_config.json` and the existing runner. The
   baseline entry is implemented without duplicate runner-only aliases; extend
   it to the audited companion/in-process population before P0 closes. Do not
   maintain a second hand-picked list of passing scripts.
   Include MVP semantic fixtures in real test discovery where currently only
   the focused Python suite reaches them.
4. Extend `lambda_mir_mvp.py` with a proposed
   `--variant typed|untyped|both` selector. Key cases by suite/name/variant and
   use the chosen source's own `.formatted.txt`/`.txt` oracle. Preserve the
   current typed default and source-manifest verification.
5. Freeze all 77 untyped entries and their transitive imports/data. Record
   whether an untyped entry imports annotated helpers; canonical mixed entries
   remain in the population, but are not advertised as fully annotation-free.
6. Capture current Full, current MVP and starting-MVP controls with matching
   source/build identity. Measure validity first, then timings for supported
   rows. An MVP rejection is an uncovered row, not a zero-duration result.
7. Produce a feature matrix: AST kind/contract, Full route and symbol, MVP
   route or rejection, positive/negative/GC fixture and implementation phase.
   Produce a failure census with reproducible commands and root-cause groups.

**Exit:** every discovered test and registered benchmark is accounted for;
compiler identity proves the intended lane ran; Full delegation is an explicit
source-backed list; untyped failures and timings are measured rather than
inferred. Validate runner/oracle selection with both a scalar entry and an
import-heavy text entry before the broad sweep.

### P1 — Complete boxed values, expressions and control

**Changes**

- Give every current scalar domain a correct boxed lowering: sized integers,
  nonfinite numbers, decimal, datetime, symbols, binary, strings, type/pattern
  values, null and Error. Reuse numeric and scalar-home primitives.
- Complete unary/binary operations, comparisons, membership, coercions,
  vectorized operations and truth handling through the shared kernels.
- Implement functional multi-value collection and procedural last-result
  behavior, lists versus arrays, elements/attributes and spread, ranges,
  decomposition, loop forms and callable-local control transfers.
- Complete recovery/filter handling and contract outcomes without losing Error
  identity. Audit declared binding/assignment behavior against current
  **S11.4.1v5**, rather than retaining older block-skipping assumptions.
- Make generic Item lowering the complete path for unknown types. A failed
  optimization proof selects it, rather than rejecting a valid program.

**Exit:** each family has positive, negative, nullable/error and mixed-value
cases in the real harness. Forced and seeded poisoned-GC runs match ordinary
execution. No semantic failure in these families remains waived as legacy.

### P2 — Calls, closures, objects and modules

**Changes**

- Complete static named/default/rest/spread/pipe argument normalization and
  evaluation order using the shared call analysis. Preserve specified errors
  for dynamic named calls and dynamic `var` signatures; accepting invalid
  syntax is not feature parity (**D7.2.2**).
- Add general function-value calls through the canonical dispatcher, retaining
  arity, effect color, return companion, resource and native callback rules.
- Implement nested definitions, captured closures and proven confined nested
  procedures from the shared capture plan. Cover recursion and mutual
  recursion with precise environment/scalar ownership.
- Complete nominal object construction, fields, inheritance, bound methods,
  receiver admission and method calls through existing object mechanisms.
- Complete public/private imports, imported values/types/functions, module
  initialization and side effects, package entry calls, module state and
  callback lifetime. Initialization executes once under both compilers.
- Keep public/dynamic boundaries boxed as required; preserve direct native
  edges where proofs exist. Do not route an imported hot helper through Full
  MIR merely because MVP is missing its body lowering.

**Exit:** nested/imported callbacks, default side effects, late callbacks after
GC, closure snapshots, legal live borrows and invalid borrow/effect cases pass.
An import-heavy untyped benchmark executes with audited MVP/delegated routes
through its entire import closure.

### P3 — Remaining language surface and GTest closure

**Changes**

- Complete multi-source/optional/keyed queries, joins, `let`, where/group/order,
  limits/offsets, windows and sequence construction. Evaluate effect/error
  producers even when the result is discarded; use the correct append mode.
- Complete structural/nominal match and type values, recursive/constrained/
  dependent/binary contracts, pattern operations and runtime type construction
  through the existing type/validator machinery.
- Complete path/parent/root/occurrence navigation, document construction and
  edits, views and handlers, resources and the remaining system-function ABIs.
- Integrate task creation, waits, messaging, cancellation, suspension and faults
  through the existing runtime. Retain audited Full interpreter delegation
  where it exists. A feature Full compiles needs MVP lowering or a shared
  semantic producer; its missing implementation is not an allowed delegation.
- Close the remaining P0 matrix rows and MVP semantic failures. Test negative
  inputs for proper diagnostics instead of disabling checks to obtain parity.

**Exit:** the entire applicable `test_lambda_gtest.exe` run passes in its MVP
compiler lane, as do procedural/runtime/concurrency semantic companions and
all MVP behavior/GC fixtures. The normal Lambda baseline passes separately.
Record expected negative tests as passes only through the actual harness's
oracle. No added skip, parked failing fixture or broad interpreter fallback
can satisfy the exit.

### P4 — Untyped scalar and call specialization

**Changes**

- Reuse shared AST inference, use/def indexes, call graph and function analysis.
  Converge recursive components deterministically. Distinguish declared
  contracts, effective body facts, entry-shape predictions and emitted carriers.
- Plan bounded raw variants using complete exact semantic-shape keys
  (**D7.3.1**). A private closed function with one proven shape can use its raw
  entry directly; open callers enter `_b`, whose guard selects a variant or the
  complete boxed body. Union/`any` canonical operands remain Items; do not turn
  them into source-level admission tests. Closures/variadics stay boxed where
  required by **D7.3.4**.
- Carry proven numeric/bool/string result domains through locals, branches,
  loop joins, direct calls and registry result relations. Handle mixed numeric,
  nullable, Error, nonfinite and overflow cases on their real semantic paths.
- Expand range proofs, native arithmetic/branches, bounded inlining and safe
  self-tail handling using the existing analyses. Failed proofs remain boxed.
- Add untyped adversarial tests: a second caller with another type, numeric
  widening after an assignment, recursive widening, a late Error and an open
  imported call. Exact-guard misses must preserve boxed behavior and effects.

**First benchmark batch:** `r7rs/{fib,sum,sumfp,tak,ack,fibfp,cpstak}`,
`kostya/collatz`, `julia/iteration_pi_sum` and scalar-heavy Mandelbrot cases.

**Exit:** every scalar batch entry runs unchanged, produces its own golden and
beats Full in a release screen. MIR/profile evidence explains remaining boxes
and call boundaries. The complete semantic gate remains green.

### P5 — Untyped collections, shapes, strings and query kernels

**Changes**

- Retain binding-local inferred array element/rank facts and exact physical
  guards. Reuse typed access primitives without writing inferred contracts or
  certificates onto containers. Shape changes, view creation, unknown writes,
  reentry and COW replacement invalidate the affected witnesses.
- Optimize untyped array access/update/iteration, native numeric buffers,
  construction, indexing and length. Hoist only guards whose dataflow lifetime
  is proven; reload movable buffers after safepoints.
- Use static literal/receiver shape facts and guarded field access for untyped
  maps/objects. Open shape misses use shared property kernels. Do not add
  property/callee caches or runtime type feedback.
- Extend ownership-based snapshot elision, RMW handles, borrowed parameters
  and call inlining to untyped values under the same shared COW analysis.
- Reduce string construction, indexing, split/search/replace, query output,
  sorting/grouping and imported-helper overhead with general source-shape or
  demand proofs. Preserve Unicode, option, Error and sequence behavior.
- Profile all unresolved benchmark families together and batch fixes by root
  cause. Audited runtime helper changes are allowed; benchmark-specific
  recognition or replacement of a benchmark algorithm by C is not.

**Benchmark batches**

| Batch | Initial cases | Main question |
|---|---|---|
| Numeric arrays | array1, sieve, spectralnorm, all nbody ports, matmul, FFT, quicksort | Where do inferred lanes get erased or repeatedly guarded? |
| Recursive/allocation | nqueens, queens, permute, paraffins, binarytrees, gcbench, triangl | Which copies, result containers, roots and calls are required? |
| Open records/calls | Richards/DeltaBlue ports, Havlak, CD, JSON, Splay, HashMap, raytrace3d | Which shapes/callees are statically knowable, and where are safe boxed misses needed? |
| Text/IO/query | Base64, regexredux, revcomp, knucleotide, Julia formatting/parsing and all text entries | Are time and allocation spent in kernels, construction, imports, or generic dispatch? |

These are tuning batches, not a reduced final benchmark population. Every
registered entry, including cases not named in this table, remains mandatory.

**Exit:** all 77 untyped entries are correct and measured; every remaining Full
gap has a profile/MIR explanation and a bounded fix or remains explicitly open.
All 77 typed entries remain in regression checks after shared hot-path changes.

### P6 — Final qualification and handoff

1. Freeze the candidate sources, transitive test/benchmark inputs and matching
   release/debug manifests. Complete semantic/GC gates before quiet timing.
2. Run the whole compiler-aware Lambda GTest gate and normal Lambda baseline.
   Native helper/GC changes require their affected runtime unit gates as well.
   Reused unchanged test results must have explicit source/binary provenance;
   they cannot stand in for the required MVP GTest run.
3. Run the complete typed and untyped paired release campaigns described below.
   Complete prespecified confirmations; retain primary and repeat results.
4. Verify every benchmark used the requested compiler throughout relevant
   modules and hot bodies. Interpreter delegation must match the audited
   feature routes; generated/generic/kernel/delegated counts stay visible.
5. Publish coverage, failure census, test commands/counts, raw timing samples,
   per-row confidence bounds, unresolved rows, build/input hashes and the final
   regression analysis. Update this checkpoint and the parent design record.

**Exit:** all three §1 gates pass. If any required test, timing or confirmation
is unfinished, or a Full control is invalid, retain partial status and identify
the exact remaining work. Do not replace per-row qualification with a favorable
geometric mean.

## 6. Files and ownership

| Area | Existing owner / planned changes |
|---|---|
| Admission and function plans | `lambda/runtime/mvp/mvp_plan.cpp`, `mvp_internal.hpp`: replace subset exclusions with complete semantic plans; consume shared inference/effect/capture facts. |
| MIR lowering | `lambda/runtime/mvp/mvp_mir.cpp`: boxed completeness, expanded syntax and guarded untyped/native consumers. Split coherent implementation units if needed; do not clone switches/helpers. |
| Shared analysis | `lambda/runtime/ast-core.*`, `build_ast.cpp`, existing type/call/COW analyses: add reusable facts only at their proper shared owner. |
| Physical emission | `mir_emitter_shared.hpp`, `mir_container_access.hpp`, `mir_path.hpp`, `mir_function_value.hpp`: shared transport, guards, roots, places and construction. |
| Runtime boundaries | `lambda-eval.cpp`, `sys_func_registry.*`, `interp.hpp`/`interp.cpp`, `activation.*`, existing document/type helpers: reuse or narrowly extend canonical behavior with audited effects. |
| Module/compiler integration | `runner.cpp`, existing MIR pass callbacks and module layout finalization: retain selector identity, ownership and import initialization. |
| Semantic harness | `test/test_lambda_helpers.hpp`, `test/test_lambda_tier_helpers.hpp`, `test/test_lambda_gtest.cpp`, related procedural/runtime/concurrency tests and `build_lambda_config.json`. |
| Benchmarks | `test/benchmark/lambda_mir_mvp.py`, `test_lambda_mir_mvp.py`, provenance/paired-bootstrap helpers: variants, transitive manifests and truthful coverage/performance status. |
| New fixtures/results | `test/lambda/mvp/` with paired `.ls`/`.txt`; versioned reports in `test/benchmark/lambda_mvp/`; temporary work only in `temp/lambda_mir_mvp/language_untyped/`. |

New C/C++ code follows the C+ convention and the `lib` collection types. No
generated build-file or parser edits are needed; registration changes go through
their source configuration.

## 7. Correctness gates and finite verification budget

### 7.1 Required test lanes

| Lane | Purpose |
|---|---|
| MVP semantic GTests | Entire discovered `test_lambda_gtest.exe` population with ordinary scripts forced to MVP JIT, plus MVP on each explicit JIT counterpart. Preserve actual T0/AUTO behavior in the other tier arms. |
| Companion Lambda semantics | Procedural, runtime errors, optimizer behavior, concurrency, std/extended suites and affected package/native callback fixtures. Record which invoke generated code and which validate shared runtime behavior. |
| Delegation/cross-boundary tests | MVP → interpreted callee → MVP callback; imported state once; wide scalar returns; `var` write-back; capture snapshots; cancellation/cleanup and GC during callbacks. |
| Ownership stress | Expanded semantic fixtures in normal, force-every poisoned and seeded poisoned modes; suspension-specific parked roots and result-home ownership where relevant. |
| Normal baseline | `make test-lambda-baseline`, preserving ordinary engine behavior and all registered aggregate gates. No Radiant baseline is requested for this Lambda task. |
| Structural checks | Existing Full emission tests retain their own expectations; MVP gets meaningful structural assertions for native hot paths, boxed fallback and rooting. Do not force both emitters to generate identical MIR. |

P0 added the central `make test-lambda-mvp` command. Its in-process and extended
coverage audit is still open. A focused CLI discovery command is:

```bash
LAMBDA_EXEC_BACKEND=jit LAMBDA_MIR_COMPILER=mvp \
  ./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*'
```

This focused command is not the full acceptance gate: explicit tier tests,
in-process compilation, companion binaries and current discovery exclusions
still need the compiler-aware integration above.

Use focused semantic/GC checks while implementing a family, one family sweep at
its boundary, and an aggregate gate at milestone boundaries. Repeat a broad run
only after relevant changes, failures or unresolved concerns. Keep structural
expectation changes separate from semantic fixes and justify each by emitted
behavior. Do not weaken goldens, skip failures or silently enlarge emission
budgets (**D8.6.1**).

### 7.2 Failure accounting

Record compile rejection, wrong output, lost Error payload, crash, timeout,
resource failure, GC mismatch, stale structural assertion and invalid control
separately. A baseline pass in AUTO is not evidence that MVP compiled a script.
A shared-runtime defect may require a shared fix, but Full's existing behavior
cannot override the formal oracle. Preserve all initial failures and reruns.

Host/environment failures need an identified cause and a separate rerun record;
they are not removed from the report. Existing failures remain visible until
fixed or explicitly outside a gate's documented population.

## 8. Performance protocol

### 8.1 Workloads and controls

- Compare MVP and Full on the **same canonical untyped source**, imports, data,
  output oracle and workload. Typed-versus-untyped ports are separate rows,
  not substitutes for a compiler comparison.
- Use a matching release build, preferably both compiler selectors in the same
  private executable. Preserve a fresh starting MVP for regression comparison.
  The historical phase3 binary/report is context, not current-tree evidence.
  Record MIR optimization/generation policy and verify native generation in
  both timed lanes; any policy-selected MIR bytecode interpretation is a
  separate execution mode, not AST delegation or a native JIT comparison.
- Freeze transitive imports and retain existing annotations found in canonical
  untyped modules. Report mixed modules. Do not erase contracts, rewrite a
  port or inject annotations to manufacture the desired result.
- Validate full stdout/results, exit status and required negative outcomes
  before accepting a timing. A wrong Full result invalidates the control;
  correct it and freeze new comparable sources before counting a win.

### 8.2 Timing and acceptance

The requested speed target is **self-reported execution time**, excluding
startup and JIT compilation, consistent with the previous comparison. Also
record process wall time and available compile/pass times as separate metrics
so an execution win does not conceal excessive compilation. Report each
benchmark's setup/output boundary; do not change it between compilers.

1. During implementation, use fixed short screens on the affected batch;
   profiling and MIR inspection are diagnostic, never performance evidence.
2. Before final sampling, prescribe 30 measured paired rounds and two warmups
   for all 77 untyped entries and the 77 typed regression entries. Randomize
   engine order deterministically and retain raw observations/orders.
3. Preselect 50-pair confirmations for close/noisy rows from independent screens
   and historical risks, including CD. Publish that selection before final
   primary samples. Retain primary and confirmation results, including an
   inconclusive repeat after a passing primary.
4. A clear win requires the one-sided 95% paired-bootstrap MVP/Full upper bound
   below `1.0` for each required row. Aim for a practical margin in tuning;
   statistical uncertainty is not a license to classify parity as a win.
5. For starting-MVP regressions, retain the two-sided 95% ratio interval and
   flag a lower bound above `1.0`. Investigate slower medians, preserve earlier
   flags and define any independent follow-up before it samples. No repeated
   sampling until a flag disappears. Unresolved regression evidence stays open.
6. A zero/timer-limited observation requires a matched, consumed-result batch
   supplement for both compilers, with identical kernel calls and a recorded
   source transform. Retain the original observations and report supplements
   separately; never treat zero as proof of zero execution cost.
7. Run timing without concurrent builds/baselines/profilers. Pause for competing
   jobs and discard the entire disturbed round. Use private binaries and source
   snapshots; never replace the shared executable during another task's tests.

Register case-specific timeouts from P0's legitimate workload measurements.
Do not reinterpret a timeout as a fast result or remove the row. If a final
candidate changes after qualification, rerun the affected gates and mark old
artifacts historical; a broad generic lowering change requires the full
performance population again.

The final report includes all rows, per-suite distributions, median/geometric
mean speedups, confidence bounds, compiler/delegation routes, invalid controls,
typed regressions and incomplete runs. Aggregate gains cannot offset one
benchmark that is slower than Full MIR.

## 9. Delivery sequence and first implementation slice

| Delivery | Required reviewable result |
|---|---|
| P0 | Population/delegation manifests, classified failure report, verified MVP GTest lane and typed/untyped runner with correct oracles. |
| P1 | Complete boxed expression/content/control paths and passing family fixtures. |
| P2 | General calls, nested/captured functions, object/module boundaries and import-heavy execution. |
| P3 | Full language matrix closure and passing MVP Lambda semantic gates. |
| P4 | Source-backed scalar/call specialization, guard-miss tests and measured untyped scalar gains. |
| P5 | Collection/shape/string/query gains across every remaining benchmark family, with typed regression evidence. |
| P6 | Frozen, complete correctness and performance qualification; published raw reports and updated checkpoint. |

Start with **P0 plus the first P1 vertical slice**: run the real forced-MVP
corpus, classify the largest rejection families, add correct boxed fallback for
those families, and demonstrate unchanged untyped scalar/import-heavy entries.
Do not begin another long full benchmark campaign before this establishes that
the harness is measuring the intended compiler and workloads.

## 10. Implementation evidence

### 10.1 P0 population and controls

The initial discovery census executed 2,006 scripts with explicit MVP/JIT on
an observed pinned executable (SHA256
`b0f1c750fd02890d114a2266a7cc063384e89fee854bc95fb8ab53c997e22c96`):
289 passed and 1,717 failed. `starting-mvp-discovered.json` and the losslessly
compressed raw log retain every result; `starting-census-summary.json` records
repeated rejection mentions, not independent feature counts. This host was
not a fresh source-matched build, so its results establish an investigation
starting point only.

The subsequent fresh release control rebuilt all 766 translation units.
`starting-build-manifest.json`, `starting-build-sources.json` and
`starting-runner-manifest.json` identify the executable, frozen sources, compiler
commands and flags. The initial untyped screen used this same executable for
MVP and Full, with one warmup and three paired samples per case, canonical
unchanged workloads, own goldens and Lambda-only execution timers.
`p0-untyped-screen.json` retains all 77 rows and failures;
`p0-untyped-screen-summary.json` distinguishes validity and median screening.

Full's five invalid controls are `beng/nbody`, `jetstream/nbody`,
`jetstream/richards`, `jetstream/deltablue` and `text/prettier_ast`. Their outputs,
assertion failures or runtime errors remain recorded. No failed engine supplies
a timing ratio or a successful performance gate. The first interrupted screen
and its missing-oracle diagnosis remain in `p0-untyped-missing-oracles.*`.

Nine untyped JetStream entries lacked goldens. New expectations came from the
unchanged scripts' success assertions; this did not change existing scripts,
workloads or oracles. `benchmark-source-manifest.json` freezes all relevant
benchmark files. `untyped-dependency-inventory.json` records each entry's local
import cone and annotated callable headers; the inventory does not claim to
count every local annotation. `benchmark-runtime-inputs.json` freezes five
actual file inputs. Those input hashes were captured after the first screen,
so that screen does not establish unchanged input bytes throughout its run.

The harness policy uses `LAMBDA_TEST_MIR_COMPILER`: ordinary script cases get
JIT, explicit JIT gets the selected compiler, and explicit T0/AUTO arms retain
their modes with the production selector (**D8.3.1**). Shared structured CLI
execution preserves specialized environment settings and argv boundaries.
Unsupported MVP admission cannot satisfy an expected language error. This is
covered by the routing test and actual positive/negative cross-tier probes in
`p0-harness-complete-routing.json`. Native helper-only tests are not evidence
that MVP lowered a language feature. In-process compiler tests and additional
companion loaders still need route-by-route auditing.

### 10.2 P1 sequence, element and lexical-scope slices

MVP now uses explicit item-position demand for lists, spread and query results.
Only direct producers receive void in item position (**S2.5.5v2**); a conditional
still evaluates its selected arm in value position. Functional blocks collect
all value contributions, while procedural blocks preserve the last value
through trailing statements (**S2.5.3v3**). Script and element content use
normalizing append; lists use sequence append (**D3.3.1**). Shared AST helpers
replace the identical Full/T0 classifiers instead of adding a third variant.
The frontend's existing classifier callers now use that same shared helper.

Ordinary elements share keyed-field staging with map literals. Fixed attributes
use descriptor fill; computed attributes and spreads use the existing literal
helpers. Content uses the canonical content append and finish functions.
Pointer conversion reuses the runtime's pointer-lane/container classification,
covering element/range and binary/datetime carriers without changing layouts
(**D2.3.4–D2.3.5**). Datetime literals publish through the existing owned scalar
ABI. File-output elements still reject explicitly until their local constructor
error region is implemented.

A list-local declaration at script scope no longer enters the module slab.
Otherwise two independent `let dt` list scopes generated the same `_gvar_dt`
symbol. Only bindings in the script's global scope get import references;
lexical locals stay in the entry frame. Imported/public bindings retain their
existing reference scheme. No name-specific exception was added.

The sequence candidate's build/source manifests identify every tested byte.
`p1-family-comparison.json` compares the same 61 cases on the fresh starting MVP
and candidate: 33 → 42 passes, nine gains, no new failures. Existing
`array_spread_element_type`, `binary_decode`, `computed_keys`,
`map_spread_override`, `spread` and `spread_star` now pass, alongside the three
new fixtures. Nineteen failures stay visible, including older fixtures that
violate the current frontend's error-bearing return contracts.

`p1-typed-goldens.log` passes all 77 typed entries under both compilers and the
three new fixtures under ordinary/forced-every/seeded poisoned GC. This is a
correctness check, not a timing result. `p1-runner-checks.log` verifies both
canonical source/oracle variants and untyped supplemental harness construction.
A transient procedural-discard mistake exposed by the family check was fixed:
a terminal return owns the result, so preceding calls keep discard demand.
The superseded failing candidate and its logs remain available.

### 10.3 Current feature and delegation inventory

Updated through P34. This records
implemented routes and focused evidence, not final qualification. Rejections
do not select Full lowering or a whole-script AST fallback
(D8.3.1–D8.3.3).

| Family | Full route | MVP state / next phase | Semantic evidence or remaining probe |
|---|---|---|---|
| Scalar literals and numeric/string arithmetic | MIR instructions and registered scalar helpers | Native and complete boxed routes, sized/compound numerics, vector/reference/subtype operators | P1/P8/P28/P30 scalar, numeric and nullable-result units; D2.3/S4/S10 |
| Lists, functional blocks, spread and root content | Local MIR collection and shared append/finalize helpers | Functional/procedural demand, conditional collection, decomposition, spread and root values | Content, decomposition and P29 functional-result units; S2.5/S12.1.1v3 |
| Arrays, shaped maps and places | Local MIR plus shared allocation/admission/COW | Boxed/native arrays, views, N-D coordinates, projected writes, nullable and caller-owned var homes | P15–P18/P27–P33 ownership, coordinate, inferred-array and admission units; D5.4/D6.3/S9 |
| Ordinary elements and attributes | Element construction and content runtime helpers | Qualified/computed attributes, content, nominal contracts, captured element snapshots | Element, nested-place and P28 member-contract units; S2.6/S9.3.1 |
| File/PDF/archive/native output | Constructor-local error region and subsystem runtime | Constructor outcomes and native result domains retained; generic virtual lengths checked | P16/P33 PDF, ZIP, archive, music, scene and Markdown units; S7.6.3v3 |
| Type values, `is`, constrained tests and match | Type expressions, runtime membership, predicate lowering | Pattern/range/recursive/constrained/binary contracts; lexical binder captures and shared env-aware admission | Predicate, binder, P23/P28/P32 member/refinement units; S11/D4.1.4 |
| Conditions, loops and queries | MIR control and collection/group/order/join helpers | While, nested products, optional/keyed/hash/cross/grouped joins, ordering/windows, traversal and nonlocal transfers | P21/P22/P29 traversal/query units; S14.1.1v2/S14.1.3 |
| Declared binding/error outcomes | Shared admission and local outcomes | Error-union transport, normal-lane conversion, recovery joins, lazy declaration/call source labels | P27/P28/P30/P32/P33 outcome and boundary units; S11.4.1v5/S7.4.4 |
| Named/default/rest/spread/pipe calls | Shared call analysis and MIR entries/adapters | Ordered binding/defaults, variadics, named/static and boxed/dynamic calls; inferred types create no contract | P19/P27/P32 binder/default, variadic and call-source units; D4.3.1/S12.3 |
| Nested closures and function values | MIR environments and boxed dispatcher | Anonymous/named captures, recursion, confined live borrows and escaping snapshot ownership | Capture corpus, P16/P23/P25/P31 units and negative escape checks; D7.2/D5.4 |
| Nominal objects/methods/modules | MIR and shared module/object runtime | Lexical methods, bound receivers, aliases, inheritance, nested declarations, imported and JS exports | P24–P26 methods/aliases/COW plus module/bridge units; S11.3/D9.3.2 |
| Views, edits, events, references and CRUD | MIR plus shared subsystem C ABIs | Native templates, handlers/state, edits/navigation, transactional opens and cleanup | P17/P18/P20 native view/edit/event and CRUD/resource units; D6.3/S9 |
| Tasks, waits, messaging and cancellation | Shared activation/scheduler ABIs | Native task launch, scoped cleanup, select staging and suspended native stacks | Task corpus, P20/P31 handler/select units; S13.1.2v2/S13.1.3v2 |
| Calling an already interpreted Function | Shared boxed dispatcher, then `interp_call` / `interp_call_borrowed` | Dispatcher reused; P34 supplies retained T0 state and preserves escaped/local fault cleanup | `MvpCallsInterpretedFunctionAndNativeCallback` passes all three GC modes; D7.4.2/D8.3.1 |
| MIR bytecode interpretation | Full can select MIR's interpreter explicitly or for large O0 input | Distinct from the AST interpreter and from unsupported-feature delegation | Record the executed mode; no native-speed claim for this route |

The audited existing interpreted-Function dispatch is conditional on the actual
`Function::entry_abi`; it upgrades a hot function before selecting the native
prototype. This does not authorize routing arbitrary rejected MVP nodes to T0.
There is no unrestricted unsupported-node delegation. The P33 source audit of
the admission switch covers the public Lambda AST families listed above;
allocation failures, invalid analysis invariants and foreign-frontend nodes
remain explicit errors. Final discovered/native gates must still qualify the
entire current population.

The following restrictions are not missing MVP lowerings:

- Dynamic named and dynamic var/inout calls retain S12.3.2's rejection. Calls
  retain the shared 16-operand limit (S12.3.1); print has its existing separate
  variadic route. Spread is a value operation, not argument expansion
  (S12.3.5v2).
- Thread/process `start` modes remain shared frontend exclusions; the existing
  implemented mode is a shared-context task (S13.1.3v2). Foreign-frontend
  C-style/do-loop AST forms are not public Lambda loop syntax.
- The CLI invokes a zero-argument procedure main. A pure function named main
  is an ordinary function (P32). Parameterized procedural CLI entries and the
  registry's unimplemented `set` entry have no established Full route here.
- The formal specifications' marked partial features remain partial; this
  compiler parity task does not invent a language surface absent from all
  three execution tiers.


### 10.4 Callable values, operators and ordered patterns

`p1_pattern_membership` includes the following source-backed additions:

- Zero-capture named definitions can be passed, returned, stored and called.
  A single shared-index scan finds actual value references by definition
  identity. Lambda need not populate `AstFuncNode::entry`, so relying on that
  field alone missed escapes. Escaping definitions use checked Item parameters
  and the C-reachable companion transport; direct-only definitions retain
  their native entries (**D7.2.2, D7.4.2**). Callable/type definitions are not
  local storage and cannot enter borrow/snapshot-elision reads.
- The shared dynamic dispatcher retains argument slots with verbatim array
  append, callee-Error ordering, colour checks and a caller-owned scalar home
  (**D7.2.2, D6.2.3, S12.1.4v4**). No new AST fallback is introduced. Named
  and `var` dynamic-call restrictions remain as ruled by **S12.3.2**.
- Bare builtin references use the same publication helper as Full, preserving
  identity, names and colours. The existing runtime publishes these as foreign
  identity values, so dynamic Core invocation rejects; this implementation
  does not claim builtin callback adapters.
- Power, keyword vector comparisons, subtype relations and reference equality
  use shared semantic kernels. The two MIR emitters share the binary helper
  mapping and the value-pattern physical test. Symbolic scalar comparisons
  keep their existing behavior (**S10.2.2v2, S11.1.4**).
- Literal, range, union, structural and first-class patterns use membership or
  equality according to the pattern value, accepting only `BOOL_TRUE`.
  Match evaluates the scrutinee once and scopes nested `~`/`~key` correctly.
  Ordinary nonconstrained handler filters use that same test and preserve an
  unmatched input (**S11.2.1, S10.1.3, S7.6.9**). Constrained predicates remain
  rejected pending their existing callable-environment lowering.

A family mismatch exposed an older MVP semantic error: exact native carrier
checks had been substituted for membership. **S4.4.1** includes `int` in
`float`; **S11.2.1** selects the first matching arm. Numeric and range-to-array
membership now retain the shared kernel, while exact native dispatch guards
remain unchanged. `match_expr` and three existing handler cases newly pass.

The old `mir_mvp_scalar_outcomes` golden expected `scalar_ordered(3)` to skip
its earlier `case float` and select `case int`. That expectation contradicted
both rulings. Current MVP, current Full and fresh starting Full all produce
`false`. The golden now records `false`, and the source comments its reason;
the case/order/workload were not changed. The original golden, hashes and all
three controls are retained in `scalar-outcomes-oracle-correction.json` and
its referenced artifacts. The initial 75-case run remains 50 passes/25
failures under its original oracle; a separate 2/2 rerun verifies the corrected
scalar case and `match_expr`. Do not rewrite the original census as passing.

All seven P1 fixtures and three P2 fixtures pass normal, forced-every/poison
and seeded/poison modes on both compilers (`p1-pattern-membership-gc.log`).
Open-var remains MVP-only in that focused Python check because Full still
mutates a simultaneous plain snapshot. All 77 canonical typed entries pass
both compilers (`p1-pattern-membership-typed-goldens.log`). The latest completed
untyped rerun, on the preceding `p1_patterns`, remains 64/77 with the same 13
failures and no new failures among the original 59 correct cases. None is
performance qualification.

The compiler-route audit also found that extended discovery already uses the
shared CLI helper. DOM-node native data tests do not compile Lambda scripts.
Security runtime and concurrency tests call `run_script_mir` in-process with
explicit tier overrides; they still need scoped compiler selection and a
source-matched native test build. Their native helper passes cannot be counted
as MVP lowering evidence. The baseline make target is implemented, but its
complete applicable population is not yet qualified.


### 10.5 Membership proofs, query windows and performance debt

The first subtype-correct candidate used generic membership even when a
producer proved its successful domain. The typed SHA-1 screen exposed its
cost. MVP now uses the shared value-set relation
`lambda_type_contract_is_subtype` to discharge that test, with the Error arm
kept separate. This relation does not rely on boundary conversion. Unknown
numeric/range domains still use membership; native entry guards keep their
exact-carrier meaning (**S4.4.1, S11.1.4, D4.3.1**). The ordered-float-arm
oracle remains correct. The SHA-1 body has the starting instruction count
again; its retained MIR diff changes embedded host addresses, not helper-call
counts. Fib's inspected body is identical. Towers' body grows and changes
inline callees; its screen flag remains an investigation, not attributed to
noise (`proof-emission-comparison.json`).

Single-source query `let` clauses now run in row scope before `where`, and
`limit`, `offset` and `limit last` trim the ordered stream before its one list
finish (**S2.5.2v2, S14.1.1v2**). Both MIR emitters use the same physical
finalizer and the existing `for_window` helper. Offset then limit are evaluated
through each emitter's value hook; sorted output stays precisely rooted across
those evaluations. No new collection/window semantic implementation was copied.
Multiple sources, joins, optional sources and decomposition remain open.

`p1-query-windows-std.json` passes seven real standard-library cases: row lets,
for clauses, grouping/windows, exponent, vector arithmetic and match. All 77
typed entries pass both compilers on that exact release
(`p1-query-windows-typed-goldens.log`). The new fixture tests empty/singleton
collapse, invalid dynamic counts, ordering and retained out-of-band floats.
MVP passes all three GC modes. Full fails the seeded poison mode: its retained
first float becomes zero. Fresh starting Full and the preceding Full candidate
reproduce the same defect (`query-windows-full-seeded-controls.json` and
`query-window-values-probe.json`). The valid oracle and failed Full subtest
remain in place; the root cause is still open.

`p1-typed-regression-screen-summary.json` records all 77 valid typed rows,
18 flagged three-pair intervals, Sumfp's subresolution observation and four
whole disturbed rounds discarded by the runner. No Radiant/C2MIR timing was
run. `typed-confirmation-selection.json` preselects 22 typed follow-up rows
from those independent flags and the existing CD/Base64/DeltaBlue/timer debt;
it does not contain final samples. After the actual membership proof change,
the separate SHA-1 diagnostic measures 23.987 ms versus 23.973 ms, ratio
1.0006; its two-sided interval is approximately [0.964, 1.080]. Preserve both
screens and every earlier flag. No repeated-sampling pass or no-regression
claim is made.


### 10.6 Multiple-source queries and references

The current tree factors single-source iteration into one recursive source
walker. It evaluates a dependent inner collection once per enclosing row,
retains each source snapshot, and applies row lets/filtering at the complete
row before the existing ordering/window finish (**S9.2.3, S2.5.2v2**).
Straight procedural products are included. Grouped products, joins, optional
sources and explicit early-control transfers still reject with specific
reasons until their row/cleanup lowering is implemented; the broad language
gate remains open. `mir_mvp_query_sources` has its own golden and covers
dependent sources, map keys, Unicode text, empty sources, ordered windows and
procedural accumulation. Both compilers pass its three GC modes. Five selected
standard-library query cases pass; all 77 typed entries pass both compilers
on the subsequent `p1_query_products_top` candidate.

Reference forcing and address lookup now call the shared `fn_force` and
`fn_address_of` helpers, with their GC/reentry effects included in planning
(**S2.4.3v3, S2.4.4**). The shared unary helper mapping serves both emitters.
`temp` installs context-owned documents and does not borrow a caller variable;
its ordinary one/two-argument forms are admitted. The new reference fixture
passes both compilers in all three GC modes. All 77 typed entries pass both
compilers on `p1_references`; the untyped correctness sweep is running.

The subsequent verified source slices add type-query expressions, inline
string/symbol pattern values and computed path keys (§10.7). Pattern-bearing
contracts remain separate pending their physical representation audit.

### 10.7 Type queries, navigation and open writes

`p1_query_pattern_paths` adds type queries through `fn_query`, compiled inline
string/symbol pattern values, and computed paths. Both MIR emitters now use
the same computed-path physical helper (**S8.2.4v3, S11.1.2v4, S2.4.2v5**).
Its new fixture passes both compilers in all three GC modes. The broader
58-case real harness family has 25 passes and 33 failures; eight timed-out
cases remain an investigation. No timeout was excluded from that result.

Parent/root navigation shares one MIR helper and the structural lineage
classifier used by T0. Nested match contexts save/restore item, key, parent
and root separately (**S10.4.3v2, S10.5.3v2**). Ordinary stored maps have no
hidden lineage. The new fixture uses structural equality for ordinary maps;
identity equality is not a pointer comparison for those values. A relative
path's selected root retains its root step (`\\./`). The first fixture draft
used the wrong identity/path expectations; its failed probe remains in
`p1-navigation-gc.json`, followed by the corrected fixture checks. No existing
oracle was weakened.

Inferred binding observations no longer become write contracts. Unannotated
bindings have open admission, retain only proven private lanes, and widen to
Item before an open `var` borrow can replace them. Place invariance runs after
the inference/effect fixed point. Erased computed/member writes use
`index_assign_cow`, preserving actual array/map/element key dispatch and the
replacement owner (**D4.3.1, S12.2.1v3, S8.2.1v4, S9.1.3**). The first
open-contract candidate exposed an array-only setter regression in jq Mix;
the shared setter fixes it. Keep both reports. The resulting candidate passes
all 77 typed goldens under both compilers and the original corpus's same
72/77 untyped rows as Full. All new focused fixtures pass the three GC modes;
Full's previously recorded seeded window failure remains the sole failure
in that Python method.

The real harness's relative-path return case exposed missing collection
effects for path literal allocation. `p1_path_effects` plans that effect;
the five-case path/navigation/open-write slice now passes. Native security
and concurrency fixtures also use scoped compiler selection, preserving
explicit T0/AUTO arms and restoring selectors after fatal assertions. Their
two translation units pass release syntax checks; native execution is not
yet qualified.

The user approved benchmark repairs after the paired original corpus reached
the same five invalid controls in both compilers. `benchmark_repairs/before.json`
retains original source hashes and copies. The repairs in progress are:

| Entry | Root cause and repair |
|---|---|
| Beng/JetStream N-body | Mutators accepted plain snapshot arrays; add the missing caller-owned `var` modes, already present in the typed ports (**S9.1.2, S9.1.3**). Keep the simulation sizes, steps and energy oracle. |
| JetStream Richards | Pointer aliases drift under value semantics. Extract the existing AWFY untyped handle-store algorithm once for both untyped wrappers; retain 50 iterations and counters 2322/928 (**S9.1.2, S9.1.3, S9.1.5v2**). Typed kernels remain unchanged. |
| JetStream DeltaBlue | Local record/array snapshots swallowed writes. Borrow the planner and publish through its owner; keep `stay` Boolean. Consume the chain/projection result in the existing 20 rounds and check checksum 5980, as the typed port does (**S9.1.2, S12.2.1v3**). |
| Text Prettier | The untyped entry prints the formatted document, but only its checksum oracle existed. Add the complete stdout oracle already established for the same AST/printer in the typed entry. |

All six affected wrappers pass both compilers in
`benchmark-repairs-validation.json`. Corpus v2 freezes 483 files: five changed
sources and two additions. Its paired sweeps on `p1_path_effects` pass all 77
untyped entries in both compilers, with runtime input hashes checked before
and after. The original 481-file corpus and all earlier results remain intact.
Performance acceptance, including every earlier typed flag, remains open.

### 10.8 Last-index scope and native-test qualification

`last` now reads the evaluated subscript receiver once, restores the enclosing
receiver after nested keys, and works in reads, writes and projected `var`
places (**S7.2.2–S7.2.3**). The two MIR emitters share the existing structural
test that excludes a nested key's own `last`. Ordinary subscripts without
`last` add no receiver boxing. The real `phase2_last_index` case and the new
focused fixture pass; both compilers pass its three GC modes. All 77 typed
goldens pass both selectors on that exact release and corpus v2.

Private security/concurrency runners now link the source-matched release
objects and the real host broker. Their first archive link selected
`mempool.c`'s standalone weak VM stubs, so both smoke commands failed before
the first script with GC slab allocation errors. Those runs do not qualify
MVP. The corrected private link retains all CLI-image objects so the strong
VM provider is selected. Original logs/manifests
remain under `p1_last_index-native-tests/`; corrected artifacts use the
`-providers` directory. No shared executable or vendor file was replaced.

`p1-last-index-v2-typed-screen.*` completes all 77 rows and discards five
disturbed whole rounds. Fourteen rows have a two-sided interval above parity;
these are diagnostic flags from three pairs, not primary qualification.
Towers is still about 20% slower. The earlier 18 flags remain retained;
`typed-confirmation-selection-v2.json` appends the new ones to the prior
selection, producing 31 preselected follow-up rows. Sumfp is timer-limited.

### 10.9 Immutable MVP image reuse

The source-matched native smoke passes three of four security arms. The
remaining JIT arm rejects `AST_NODE_START`, exposing the still-open task
lowering; it is not waived. The original cache smoke passes its explicit T0
arm but fails the JIT image-reuse counters: `runner.cpp` deliberately disabled
artifact reuse for MVP. Full passes the three selected cache controls.

MVP now publishes through the same immutable-image and dependency-cone
protocol. Its backend key is `mir-direct-mvp`, separate from Full's
`mir-direct`, so identical source cannot select another compiler's image
(**D8.5.1**). No runtime value/layout or cache ownership protocol changed.
`p2_image_cache` passes eight source-matched native cases: eager image reuse,
MVP/Full isolation and repeat reuse, logical/dense module mapping, explicit T0
template/import reuse, MIR import-cone reuse, changed-dependency invalidation
and concurrent single-flight compilation. New/old compiler selectors are
restored by the shared native fixture guard. A new test's const-qualification
compile error was corrected; its failed source/log remain retained.

The all-77 untyped v2 `--lambda-only` screen on this release is running.
Completed rows expose large scalar/loop gaps, for example diviter around
15 times Full and mbrot around 10 times Full. Do not confuse 77/77 correctness
with performance completion. Investigate shared parameter/loop/boxing costs
across the whole unresolved population; keep declared `any` admission open
when adding private observations (**D4.3.1–D4.3.4**). All performance gates,
the broad language matrix, task/closure/pipe lowering and aggregate GTests
remain open. No final performance qualification or new fallback is claimed.

### 10.10 Untyped performance screen and private integer inputs

The three-pair `p2-image-cache-v2-untyped-screen.json` includes all 77
repaired-corpus rows with valid outputs. MVP has the lower median on 19;
17 have a screening one-sided interval below parity. The geometric mean
MVP/Full ratio is 2.226. These small samples do not qualify performance.
`p2-image-cache-v2-untyped-screen-summary.json` retains all 60 open rows
and the worst ratios. HashMap (487 times Full) needs an ownership-path
investigation; several scalar kernels retain unnecessary boxed parameters.

The next candidate adds private integer parameter observations to direct-only,
non-escaping definitions. Public/boxed, defaulted, borrowed and reassigned
parameters retain their existing carriers. All direct incoming edges are
validated, including recursive edges; a non-integer, nullable or Error
observation widens back to Item in the same fixed point as local writes.
The source admission contract remains unchanged, including implicit
`any ! error` admission (**D4.3.1, D4.3.2, S11.4.3**). The first candidate
checked only explicit `any`, so its Diviter diagnostic made no gain. That
result and source are retained; the corrected candidate includes implicit
parameter admission without altering it.

`p4_closed_inputs` also extends the existing exclusive projected borrow to
open parameter contracts. A borrow needs ownership rather than a packed
layout proof. The previous snapshot capture made each HashMap store clone
its entire array. The shared `cow_path_borrow_keys` path preserves aliases,
rooted homes, replacement publication and error writeback (**S9.1.2,
S9.1.3v3, D5.4.6**). Multiple/retaining arguments keep the established
snapshot path. The new fixture covers an outer snapshot, nested places,
scalar leaves, replacement, simultaneous plain arguments and partial error
publication.

All 77 v2 untyped goldens pass MVP; all 77 typed goldens pass both compilers.
The two new fixtures pass all 12 compiler/GC combinations. Four focused
real-harness cases pass. The content/GC matrix passes 95/96, retaining only
the pre-existing Full seeded-window defect. Source-matched seven-pair
diagnostics report Fib 2.265 ms vs Full 1.078 ms, Diviter 224.469 ms vs
264.408 ms, and HashMap 118.317 ms vs 72.525 ms. Only Diviter is below
parity in this slice. These are workload timers and diagnostics, not
end-to-end measurements or final acceptance. The earlier HashMap 34-second
result remains in its original report. Typed performance preservation,
all remaining untyped rows and broad language qualification remain open.

A subsequent candidate seeds private integer results only for closed,
unannotated definitions whose normal exits are explicit returns. Every
return edge must retain an integer success domain at the widening fixed
point; null, collected content or unknown returns retain their old ABI.
This targets the remaining boxed recursive results. `p4_integer_results`
passes all 77 untyped goldens and the semantic matrix except the known Full
seeded-window defect.

`p4_integer_effects` defers open-receiver effects until these lanes are
known. Its sweep exposed a native-tail-call bug: source admission boxed an
untyped operand and the backedge moved that Item into the native integer
register. The recursive fixture and Divrec hung; this task's affected
sweeps were stopped and retained as failed/incomplete, with exact metadata
in `p4-integer-effects-interruption.json`. The fix converts each admitted
backedge operand to its planned binding carrier, as the initial direct
entry does. No benchmark workload or timeout was changed.

`p4_private_calls` additionally preserves a proven integer carrier across
an open parameter boundary, avoiding a redundant box/unbox while keeping
ordinary Error admission for other inputs (**D4.3.2, S11.4.3**). This release
passes all 77 untyped MVP goldens and all 77 typed goldens under both
compilers. The 96-case content/GC matrix retains only the known Full seeded
window failure; all 18 callable/GC combinations and three open-var checks
pass. The expanded integer fixture covers recursive returns, nullable
returns, collected fallthrough and Error outcomes. Four real-harness
input/borrow/path cases pass.

The new all-77 untyped and all-77 typed release screens run serially, pausing
around other checkout builds. Early diagnostics show Fib 0.569 ms versus
Full 1.100 ms and Sum 0.102 ms versus 0.267 ms. These do not qualify the
remaining rows. Preserve the earlier typed 31-row confirmation selection;
Towers and other regression flags remain open until measured on this source.
No full language/GTest or performance gate has passed.

### 10.11 Exact float domains and final place admission (in progress)

The closed scalar proof now shares one incoming/return-edge walker for integer
and float candidates. Float selection requires exact carrier agreement across
all calls; integer membership in `float` does not authorize a hidden inferred
conversion (**D4.3.1, D4.3.2, S4.4.1**). Defaulted, borrowed, reassigned and
escaping parameters retain the previous entry protocol.

This review exposed an existing constant-propagation defect: a typed float
function with one literal direct caller and an escaped value use could replace
the callback's later argument with the direct caller's constant. The new
named-value regression reproduces old MVP output `1.25, 1.25` against Full's
`1.25, 2.5`; `p4-float-constant-escaping-control.json` retains that probe.
`plan_float_parameters` now excludes boxed/escaping entries. The original
pre-extension fixture is retained in `pre-float-named-fixture/`.

`p4_scalar_domains` (SHA256
`31d2dd7ca06784e0e4fe76e1845b80d3253eb8d16c58226f9522a7defb422cff`)
passes the extended callable/GC checks and all 77 typed goldens under both
compilers; the content matrix still has only the known Full seeded-window
failure. Its full untyped sweep is **74/77**, with Havlak, Splay and jq BF
rejected by early write-place carrier admission. Retain that failed census.
Provisional scalar function results had seeded locals before the producer
proof widened; write-place admission ran before the same fixed point.

The next candidate, `p4_scalar_places`, validates place structure/ownership
early and validates its physical carrier only after inference converges,
using one parameterized admission helper. The expanded fixture includes
record/array results with a scalar alternative followed by projected writes.
Its SHA256 is
`257fe88f708e92ff5504db00be46d7909dface39ab86f2f73c5c257fc4edfafe`.
All 77 untyped MVP goldens and all 77 typed goldens under both compilers pass.
The content/GC matrix still has exactly the known Full seeded-window failure;
all 18 callable/GC and three open-var checks pass. Five real-harness
input/borrow/named-value/path cases pass. This is the latest validated engine
release; no performance campaign on this float candidate has completed.
The earlier `p4_private_calls` all-77 untyped screen and then typed
regression screen are still running, pausing around a separate checkout
baseline. The binaries and source manifests are frozen, so these
results must not be attributed to the newer float candidate.

The earlier `p2_image_cache` source-matched cache runner now also passes all
eight selected cases under forced-every-allocation GC with poison enabled
(`p2-image-cache-native-forced-gc.json`). These certify that cache slice;
they are not native qualification of the newer scalar candidate.

**Next recovery steps:**

1. The `58427` and `77455` all-77 campaigns completed; their summaries are
   retained. `screen_frozen_candidate.py p4_no_value_inline` now runs the
   current complete untyped and typed screens serially. Do not start competing
   benchmark runners. Each report manifest owns its frozen release even if
   the checkout changes.
2. Summarize every untyped open row and every typed regression flag; append
   new flags to the preselected 37-row confirmation population. Sumfp's
   resolution-limited typed case still needs its matched batch.
3. Complete the new `p4_no_value_inline` screens, including all float
   kernels and typed regressions. The focused Towers flag remains open. Preserve all earlier failed
   or slower evidence. No latest-source performance win is yet claimed.
4. Resume the P1 language slices (pipes/provisos and remaining calls/control),
   P2 captures/normalization and P3 tasks. Full's pipe walk currently uses
   child-only element traversal despite **S10.1.2v4/S10.1.6** requiring the
   unified attribute/child walk; reuse the canonical iteration helpers rather
   than copying that discrepancy. A proviso also needs ordinary condition
   Error transport and the correct subject/key/parent/root bindings.
5. Before admitting `AST_NODE_START`, treat its inner argument-array syntax
   as task materialization, mark the callable boxed and exclude the inner
   syntax from ordinary incoming-argument inference, matching Full's
   prepass. Preserve real task-scope join/unwind ownership; a runtime helper
   call alone does not close this feature. Native task/security and the full
   language matrix remain open.

### 10.12 C Bool outcomes and completed integer screens

The generic builtin lowerer treated raw `BOOL_ERROR` as a successful Boolean.
The new fixture reproduces six wrong observations on `p4_scalar_places`: bad
`contains`/`starts_with`/`ends_with` input, `not`, conditional selection and
postfix propagation. Full produces the correct Error outcomes; the exact
probe remains in `p1-builtin-bool-before.json`. MVP now consumes the registry's
instantiated result relation, matching Full's existing policy: only a plain
error-free Boolean result can retain the raw lane, while an error-capable
result or `^` uses the existing `bool_call_outcome` helper (**S7.8.1, S7.9.3**).
No runtime helper, type model, fallback or oracle changed.

`p1_builtin_bool`, SHA256
`e8089c1c2f47fad05c2c129c48411ed2cbab67bd0dbbeff6da3beebb733f8695`,
passes all 77 untyped MVP goldens and all 77 typed goldens under both
compilers. All 18 callable/GC and three open-var combinations pass; the
102-case content/GC matrix retains only the pre-existing Full seeded-window
failure. Six focused cases pass in the real GTest harness. The broader
language matrix and native task/security gates remain open.

The older integer-only `p4_private_calls` all-77 screens completed. Untyped
outputs are valid on all rows: 22 lower medians, 21 screening intervals below
parity, geometric mean MVP/Full 1.648 and 18 disturbed whole rounds discarded.
All 56 open rows remain in `p4-private-calls-v2-untyped-screen-summary.json`.
This improves the previous screen's 19/17 and 2.226, but the target is open.
Its typed screen flags eight possible regressions: Fib, Diviter, Permute,
Storage, Towers, Mbrot, Microdiff and jq Records. Towers is 23.9% slower than
starting MVP. Five disturbed rounds were discarded; Sumfp remains
resolution-limited. Previous flags are retained. The append-only
`typed-confirmation-selection-v3.json` now has 33 cases. These three-pair
results do not qualify performance or apply to the newer float/Bool release.

The latest `p1_builtin_bool` all-77 untyped and all-77 typed screens are
running serially (session `77455`), with a source-frozen release and corpus
v2. No C2MIR or Radiant campaign ran.

### 10.13 No-result procedure inlining and preserved control failures

A small procedure with only declarations/local writes has no normal scalar
result. The shared `ast_proc_block_last_value` classifies this; any explicit
return owned by the same function clears the fact. Nested definitions use
their own owner. The inliner may now consume such a procedure's Item result
without treating it as an unknown wide scalar. It still retains the null
result, ordinary Error outcomes, borrow publication and scope exits
(**S2.5.3v3, S12.1.2v3, D6.2.3**). No return ABI or user contract changed.
This addresses the push/pop inlining topology behind Towers without naming
that benchmark in compiler code.

`p4_no_value_inline`, SHA256
`00969c5b1e2aaef66a6e579e054e8fe10dd875926c380bad0345f1c36181b44a`,
passes all 77 untyped MVP goldens and all 77 typed goldens under both
compilers. Seven focused real-harness cases pass. All 54 MVP content/GC
combinations, all 18 callable/GC combinations and three open-var modes pass.
The four-method command records four failed Full subtests: the known
seeded-window defect and all three modes of the new scalar-var snapshot
loop. Its checks preserve null replacement, partial-store errors, early
returns and a wide value saved across repeated calls at one inline site.

`p4-no-value-scalar-store-controls.json` reproduces the new Full mismatch on
`p1_builtin_bool`, this candidate and the freshly rebuilt starting release;
MVP passes each control. Removing the preceding deliberate OOB error does
not fix Full; explicit T0 passes (`p4-no-value-no-prior-error-controls.json`).
The normal-sized float probe also fails Full's saved-value check
(`p4-no-value-normal-var-controls.json`). The exact Full root cause remains
unresolved. Do not weaken the golden or claim this overall command passed.

The nine-pair Towers diagnostic is 0.156 ms versus Full 1.237 ms and starting
MVP 0.131 ms. Its MVP/starting ratio is 1.191, two-sided interval
[1.055, 1.366]; the typed regression remains open. The one-sided MVP/Full
upper bound is 0.139; this focused diagnostic is not complete performance
qualification. No disturbed round was discarded.

The completed `p1_builtin_bool` all-77 screen has all outputs valid, 21
lower medians, 21 screening intervals below Full, geometric mean 1.355 and
three disturbed rounds discarded. Its typed screen flags nine rows:
Divrec, GCbench, Binarytrees, Permute, Towers, Regexredux, Formatted Output,
Microdiff and jq Tree; Sumfp remains resolution-limited. The append-only
confirmation population is now 37. These results belong to that preceding
release, not this inlining candidate. Primary and confirmation campaigns,
the complete language matrix and aggregate MVP GTests remain open.

### 10.14 Provisos, variadic print and native scalar borrows

MVP now lowers the single-value proviso (**S10.1.5v3**) with Full's shared
physical subject/result helpers. The subject is evaluated once; `~key` is
null; the enclosing parent/root survive; a rejected predicate gives null.
The nullable result retains its emitted carrier, so consuming it in numeric
comparisons does not invent an unknown Item-to-Bool conversion. The new
`mir_mvp_provisos` fixture includes implicit fields, nested matches, ordinary
predicate Error, short-circuiting and a cold float held across allocation.
Sequence and aggregate pipes still require their own lowering.

`mir_mvp_scalar_var_lanes` exposed two distinct native borrow defects: the
precise caller cell needs a tagged Item, and an initializer's singleton range
cannot survive a var call. Native int/Bool parameters are now admitted;
shared publication converts local lanes to Items, call setup boxes the cells,
and reload converts to the caller's carrier. Range proofs exclude borrowed
bindings and escaping entries; float constant proofs also exclude borrows.
Dense array entries retain their existing no-replacement policy and decline
scalar var parameters. The fixture exercises inline/native calls, forwarding,
projected field COW, partial-error publication and an escaping integer reader
(**S9.1.3, D4.3.1–D4.3.3, D6.1.1, D7.4.2**). Native float and other scalar
var contracts still need their ownership/transport work; this is not complete
var-parameter coverage.

Variadic and zero-argument `print` use Full's extracted physical lowerer.
Separators precede evaluation of each following argument, preserving observable
source order; positional values and wide scalars keep their ownership. The
existing single-argument MVP provider outcome is preserved; Full's existing
variadic completion remains null. No provider-failure behavior was newly
qualified. The new fixture checks nested printing, zero arguments and cold
scalar arguments (**S7.9.3, D6.2.3**).

`p1_scalar_var_proofs`, SHA256
`41f21a88ecf5c1a07144e9f0b99a04ce3b135a4370b0cfca8240394c6220ef16`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. All 18
new-fixture compiler/GC combinations pass, as do the existing callable/open-var
checks. Three newly discovered cases pass the real core harness
(`p1-scalar-var-proofs-core-slice.json`). Two earlier selection attempts are
retained: one had an incorrect relative executable path, the other used the
older runner without MVP-directory discovery and selected zero tests. Neither
is test evidence. The successful runner is `p2_discovery_gtest.exe` in the
private source-matched CLI workspace.

The broader five-method Python command is **failed**: four known Full control
subtests and 42 failures across 14 older MVP ownership fixtures. The latter
include stale acknowledgment/declaration expectations and Error comparisons;
their exact classification is not yet complete. Every one also fails both
compilers in the fresh starting release in normal mode
(`p1-scalar-var-legacy-fixture-controls.json`). No test was removed, weakened
or declared passed. The new scalar-var golden and the existing benchmark
oracles remain unchanged by this investigation.

The completed `p4_no_value_inline` untyped screen has 77 valid outputs, 23
lower medians and 20 screening intervals below Full. Geometric mean MVP/Full
is 1.341; seven disturbed whole rounds were discarded. All 57 open rows remain
in `p4-no-value-inline-v2-untyped-screen-summary.json`. This three-pair screen
is not final qualification. Its typed campaign is still running as PID 90771's
serial campaign; recover process metadata before launching any competing
measurements.

**Recovery:** `p4_explicit_top_returns` is built (SHA256
`88186008b8c52d7d4a1e04362eaf7a23c2cf271a1f9852632bd639769b9c34ea`).
It extends the existing closed-result proof to explicit top contracts while
retaining public admission and the Error companion (**D4.3.1, D7.4.2**).
Mixed/nullable/unknown returns widen back to Item. Its extended fixture,
complete typed/GC command (session `7622`) and all-77 untyped correctness
sweep (`60812`) are running. Do not claim this candidate validated yet.
Next finish these checks and the preceding typed timing screen; retain all
old flags in the append-only confirmation selection. Then continue the open
language slices and performance rows. Full language, native and aggregate
MVP gates remain open; no commit or completion is claimed.

### 10.15 Closed top results and readonly primitive-array inputs

The existing closed-result fixed point now also accepts explicit `any` top
return contracts. Every normal return must retain the exact inferred int or
float carrier; mixed, nullable and unknown returns restore the Item ABI.
Public return admission and ordinary Error transport remain unchanged
(**D4.3.1–D4.3.3, D7.4.2**). The extended closed-parameter fixture covers
recursive int/float results, explicit top contracts, nullable/mixed outcomes
and returned Error. `p4_explicit_top_returns` passes all 77 untyped MVP and
77 typed goldens under both compilers; its four-method command retains the
four Full control failures known at that point.

Readonly closed parameters can also retain a primitive-array element domain.
The observation is unbounded and does not install a source contract or an
array certificate. Every incoming edge must prove the same primitive element
kind; int-to-float subtyping cannot merge element carriers. The existing
`mvp_parameter_borrowable` indexed-use audit rejects mutation/whole-owner
escape while allowing scalar child reads. An erased child-retention flag
alone was too conservative: the first `p4_closed_array_inputs` candidate
passed correctness but left the example reader on generic `fn_add` and
`fn_len_checked`. Reusing the ownership audit produces native arithmetic
and header length reads while retaining checked index misses. Both MIR dumps
are preserved; no benchmark name or workload constant appears in this proof
(**D4.3.1–D4.3.3, D5.4.4, S9.1.2–S9.1.3**).

`p4_closed_array_borrows`, SHA256
`d8dcb7c3994ee79fa1f3d3bf412577a5a60f8b3087bee5a8fa6cd1c0056e9575`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. All 66
MVP content/GC, 18 callable/GC and three open-var combinations pass. The
closed-array fixture also checks different extents, mixed int/float caller
arrays, open element replacement, escaped calls and Error argument transport.
Its extended five-row oracle passes MVP in all three GC modes. Full gets two
older observations wrong (open replacement and the escaped call), including
on the fresh starting release; exact controls remain in
`p4-closed-array-full-controls.json`. The latest four-method command therefore
has seven Full failed subtests, not an overall pass. The 14 older ownership
fixtures from §10.14 remain existing aggregate-gate debt; none was removed.

The preceding `p4_no_value_inline` typed screen is now complete: 77 valid
outputs, 16 possible-regression flags, four disturbed whole rounds discarded
and Sumfp timer-limited. Array1, Sieve, Spectralnorm, AWFY Nbody, Fibfp, Ray,
Cpstak, Binarytrees, Pidigits, Towers, Navier–Stokes, Iteration Pi Sum,
Formatted Output, Splay, Hyphen and jq Mix are flagged. These three-pair
intervals do not confirm regressions. The append-only
`typed-confirmation-selection-v5.json` has 44 rows, retaining every earlier
flag; primary/confirmation campaigns remain open.

**Recovery:** the current engine source matches `p4_closed_array_borrows`.
A fresh real-harness discovered-script census is running in private workspace
`workspace-census-closed-array-borrows` (session `37441`). Its Lambda fixture
tree is copied/frozen; the remaining linked test roots are hashed in the
7,525-file `p4-closed-array-borrows-census-inputs.json`. Use the completed
report to rank language failures; do not qualify in-process/native GTests
from this CLI-only census. Its runner is `p2_discovery_gtest.exe`, ordinary
scripts select actual MVP/JIT, and the requested worker limit is one.

The previous performance campaign completed. A new all-77 untyped then typed
screen on this exact frozen release is queued/running as session `42810`;
`--pause-for-jobs` prevents measurements during this census or the separate
checkout baseline. Do not launch another benchmark runner. Summarize its
reports when complete and append any typed flags without dropping old rows.
The language, native and aggregate gates remain open. Next continue pipes,
captured callable environments/normalization and the remaining control/task
routes. No interpreter fallback, C2MIR/Radiant campaign, commit or completed
goal is claimed.

### 10.16 Canonical sequence pipes and lexical activation contexts

MVP mapping/filter pipes now share `em_pipe_walk` with Full. It uses the
canonical `item_keys`/`iter_len`/`iter_key_at`/`iter_val_at` helpers, retains
precise source/member/key/output roots, lifts scalars and finishes through
`pipe_end`. Full's duplicated type-specific walker is removed. T0 uses the
same canonical iteration functions. This corrects the old child-only element
walk under **S10.1.2v4/S10.1.6**: attributes precede children. Map keys, text
kinds, empty/scalar/list results, nested parent/root/key bindings and ordinary
predicate Error are covered by the new fixture. C Bool truth classification
is explicitly zero-extended before exact `BOOL_TRUE` selection.

MVP still rejects aggregate pipes pending argument injection. Sequence bodies
with nonlocal exits, potentially failing local boundaries or var-place calls
also stay rejected until resource-unwind lowering owns their key lists.
Unsupported syntax remains an error, with no interpreter/Full fallback.

`p1_sequence_pipe_roots`, SHA256
`224a953758c772df7e26618d5c1f92c7590eb58a117fce39924eaff51b6d481c`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. The
initial valid pipe fixture passes all nine MVP/Full/T0 × GC combinations.
Earlier probes used invalid element/statement-block syntax and are retained
as failed construction attempts, never test evidence. The final fixture uses
ordinary element content and a value-producing callback.

A separate probe then found lexical `~key` leaking into an ordinary called
function: MVP's inlined body and T0 answered `[false, false]` for a function
defined outside the pipe; Full answered `[true, true]`. Exact controls remain
in `p1-pipe-inline-context-before.json`. Per **S10.1.3**, inlining now saves,
clears and restores implicit context, while T0's function entry installs a
scoped context barrier before defaults/admission/body evaluation. Method and
constraint candidates still bind their own subjects. This changes no MIR
instruction in ordinary context-free inlining.

The current `p1_pipe_activation_context`, SHA256
`fb14dc147e7e6b4ae5f37dc4a3ee20cb60c012474c102da17b8677ca9773a56c`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. All 69
MVP content/GC combinations pass; the corresponding four-method command
retains the same seven Full failed subtests. The extended pipe fixture,
including the activation-boundary counterexample, passes all nine three-tier
GC combinations (`p1-pipe-activation-context-three-tier-probe.json`). The
focused real pipe family is MVP 5/8 versus Full 8/8. Aggregate injection blocks
`pipe_spread` and `pipe_that_kind`; `typed_array_pipe` additionally exposes
numeric comprehension finalization (`shape`/`reshape`). Do not alter those
oracles or call the family complete.

### 10.17 Timing monitor correction and active census

The array-release campaign began measuring while its private census was still
running. The monitor only classified generic `.exe` programs above a 5% CPU
threshold; a GTest supervisor between worker bursts could therefore be missed.
The census and campaign have distinct process groups, so group exclusion was
not the cause. Private `*_gtest.exe` supervisors, `test-batch` children and
Python unittest/pytest suites now block timing regardless of transient CPU.
`test_idle_test_supervisors_block_benchmarking` passes with zero-CPU private
supervisors, an excluded own process group and an idle desktop process.

Only this task's timing process group 8378 was stopped. Its 11-row partial
report remains unchanged, with a hash and explicit invalidation in
`p4-closed-array-borrows-screen-invalidation.json`; session `42810` ended 143.
No foreign baseline or census was stopped. Do not resume/overwrite that
campaign or claim its rows as performance evidence. Historical diagnostic
screens also predate the monitor fix and may have missed private test bursts;
retain their flags as investigative leads, never final confidence evidence.
Clean primary/confirmation runs remain required.

**Recovery checkpoint:** current engine source matches
`p1_pipe_activation_context`; its release build, source/runner manifests,
three-tier probe, typed/GC log and all-77 untyped report are complete. No
benchmark runner is active. The older source-frozen
`p4_closed_array_borrows` discovered-script census still runs (session `37441`,
GTest PID 7022); the copied Lambda fixture tree cannot be changed by new root
fixtures. It had processed about 990 scripts at the last log check and was
working through the linked demo roots in batch 24. A busy child alone does
not establish a stuck script: the batch has 50 entries and a 60-second
per-script alarm. Preserve partial output and establish a specific failing
script/root cause before interrupting the run or changing timeout behavior.

Next finish/classify that census, implement aggregate call normalization and
resource exits, then captured callable environments and the remaining
control/task/type routes. The aggregate pipe AST is already a call with a
virtual first receiver (`pipe_inject`/`pipe_receiver`); shared
`ast_resolve_call_arguments` owns that source shape. Every incoming-domain,
range and ownership proof must consume it correctly before general user-call
injection is admitted. Reuse existing builtin and dynamic-call emitters rather
than cloning their C ABI/error/ownership protocols. Keep the 44-row typed
confirmation population and Sumfp batch requirement. Full language/native
and performance goals remain open; no commit or completion is claimed.


### 10.18 Aggregate pipes, scalar array finalization and completed census

The frozen `p4_closed_array_borrows` discovery census is complete: **713/2,077
pass; 1,364 fail**, including seven missing batch results. Exact JSON/log and
`p4-closed-array-borrows-discovered-census-summary.json` retain the population.
This is an older CLI-only candidate, not current language qualification or an
in-process/native gate. The seven missing rows need batch-root investigation;
no timeout, oracle or harness failure was suppressed. No census or benchmark
runner is active.

Per **S10.1.2v4**, aggregate pipes now lower through MVP's existing direct,
builtin and dynamic call protocols. A shared positional accessor prepends the
non-owning `pipe_receiver` without rewriting the AST. Incoming-domain widening,
integer/float constants, private arrays, borrow aliases, extents, effects,
string recipes, native numeric helpers and closed-call witnesses all consume
that position. The emitter evaluates it once; dynamic calls keep the receiver
rooted across callable evaluation. Named and spread call arguments remain
open; no new fallback was added.

`p1_aggregate_pipes`, SHA256
`75607d058124d40e93a97aa5c8a6d3445b17482d02d2ea3933c07a26f8ab22ce`,
passes the new aggregate fixture under MVP and T0 in normal/every/seeded GC.
The first new probe also pinned receiver/callee order, exposing a difference
from Full. **S7.7.1v3 explicitly leaves strict left-to-right evaluation order
non-normative**; that observation is not a proved Full defect. The new fixture
now counts producer evaluations without depending on their order. Preserve
the first source-hashed diagnostic in `p1-aggregate-pipes-three-tier-probe.json`
separately; rerun the corrected fixture before claiming its nine-way gate.
The focused real pipe family passes **8/9**, retaining the existing
`typed_array_pipe` oracle. An inadvertently broad Python discovery command was
stopped before its legacy reference checks; its partial log is unqualified.
Use explicit Lambda-only methods for this goal.

The comprehension failure comes from skipping finalization of dynamically
produced scalar elements in generic array builders. `array_end_flat` now
reuses the shared exact scalar-lane promoter while keeping nested row owners;
`array_end` retains its existing tensor promotion then calls that same flat
finalizer. MVP's generic array literals always finalize scalar storage, using
tensor folding only when the destination requires it. This keeps the physical
choice separate from language contracts (**D2.3.1–D2.3.5, D3.2.1–D3.2.2**).
The release `p1_array_scalar_finish` is building; correctness and performance
checks for that source remain open.


### 10.19 Scalar finalization validation and active ownership investigation

`p1_array_scalar_finish`, SHA256
`fecc66b6a5f65d3f72359af76b2b4710b32667a7f2e7580397681532e55def45`,
passes the real pipe family **9/9**, all **77 untyped** goldens under both
compilers and all **77 typed** goldens under both. The corrected aggregate
fixture passes all nine tier/GC combinations on its preceding release;
`p1-aggregate-pipes-evaluation-count-probe.json` records source/oracle hashes.

The four-method run has **eight failed subtests**, not a clean pass: the
seven known Full controls and a new MVP seeded `mir_mvp_query_windows`
failure. Before scalar finalization this MVP case passed. Its first saved
cold float becomes zero. Keep this regression open and keep its oracle.

The census also identified abstract numeric contracts being rejected as type
objects. Admission now uses `type_is_global_meta_type`, retaining `number`
and `integer` in the shared Item/owned-result protocol, as the canonical
router already requires (**D2.3.1–D2.3.5, S11.4.1v5**).
`p1_abstract_numbers`, SHA256
`0b8c7adcc890b4464739e13ea4d00470a6a4f31be75c37ac35c47fea6cf48fe4`,
passes its new argument/result/deferred/callable/cold-scalar fixture in all
nine tier/GC combinations. It inherits the still-open window regression.
The fixture's initial return annotations omitted possible Error; construction
was corrected to `number^`/`integer^` before the passing probe.

**Active debug source:** temporary, environment-guarded logging in
`lambda-data-runtime.cpp` and `collection_runtime.cpp`. Remove all of it
before qualification. `p1_scalar_finish_trace` used stripped release debug
macros and produced no diagnostic; `p1_scalar_finish_trace2` uses scoped
error logging instead. Its log proves the first saved value is already zero
*before* scalar promotion; the array's data pointer then moves during
allocation and its second owned scalar is rebased correctly. Therefore a
missing source-array root inside the scalar promoter is not an established
root cause. The next private release, `p1_scalar_append_trace`, instruments
the incoming scalar across append-buffer growth. `Rooted<Item>` retains a
tagged word but does not own a borrowed scalar payload; inspect this interval
before changing the precise ownership protocol. No conservative scanning,
test skip or timing claim was added.


### 10.20 Append ownership fix and stable pre-capture gate

The trace establishes the cold-float root cause. In
`p1-scalar-append-trace-mvp.log`, an incoming tagged scalar retains the same
pointer across `expand_list`, but its value changes from approximately
`3e-310` to zero. That pointer borrows the source array's movable data buffer;
`Rooted<Item>` preserves a tagged word, not its scalar payload. The shared
append growth path now calls the existing `lambda_item_adopt_scalar_home`
before allocation and retains that home through the final store. Native and
boxed growth use one helper; capacity-available appends keep their path.
All temporary trace code has been removed (**D5.3.1, D6.2.3, D6.3.1**).

`p1_append_scalar_homes`, SHA256
`b5322ed5251fea5535c006db551f5bf033c9fbc8c0065e01bd046c9f7d97b32c`,
passes **77/77 untyped and 77/77 typed in each compiler**, **75/75 MVP
content/GC combinations**, all **16 window compiler/seed controls**, and the
expanded real pipe family **12/12**. The five-method Python command retains
**six Full failures**: the three procedure-result modes and three closed-array
modes. The previously known Full seeded-window failure is fixed. Eight
preselected window seeds are now retained in a dedicated regression method;
the preceding control sweep also found older MVP failing seed 101, before
scalar finalization, so this was an exposed shared lifetime defect.

Source-matched private native security/concurrency executables were rebuilt
with strong runtime providers. The eight selected image/cache checks pass in
both normal and every-allocation GC (**8/8 each**). The full native suites and
aggregate baseline remain open. Root executables were not replaced.

### 10.21 Immutable captures and static named calls

The shared function-value helper now owns plain/closure object construction,
field counts, names, type metadata and the boxed context/companion ABI.
Full's frontend keeps its capture permission/read logic while both compilers
share the physical environment loop and owned-slot reader. Names pass the
interned `char*` expected by `to_closure_named`; the former Full branch passed
a `String*` there. MVP supplies readonly lexical snapshots, receives the
hidden environment before user operands, and gives loaded cold scalars their
own activation homes (**S9.1.4v2, D7.2.1–D7.2.3, D6.2.3**). Per-activation
bindings retain no parent MIR registers, borrowed homes or writable witnesses.
Mutable/live, imported/unindexed and recursive function captures remain
explicitly unadmitted. No fallback was added.

`p2_immutable_captures`, SHA256
`792b9e128c91e758504ff834bc90d7f45e35333a3662995af7a0066f4fd98d7f`,
passes **27/27** tier/GC combinations across the new fixture, existing
`fn_local_snapshots` and `transpile_typed_closure`. Its real closure slice is
**5/7**. The new fixture's initial reserved identifier and implicit Error
return annotations were corrected before this passing probe.

Static named calls now use the shared `ast_resolve_call_arguments` mapping in
all incoming proofs and emission, including omitted defaults, var places and
pipe receivers. The map constructor's registry names now identify its actual
fixed ABI providers (`vmap_new`/`vmap_from_array`), so MVP uses its ordinary
builtin lowering. `p2_named_capture_calls`, SHA256
`3b5485f2f85c25b02d00d3827ce5ee648a3590a00cd6d8f31575ca2e7f21b11e`,
passes the expanded real closure/named slice **10/10**. Its broader four-fixture
matrix is **28/36**, not complete: Full misorders named pipe operands (3),
T0 excludes that combination (3), and MVP loses a let-list string capture
under every/seeded GC (2). Keep each failure and oracle.

The capture GC gap is the environment's construction interval: `heap_calloc`
with tag zero does not trace initialized capture slots before a Function owns
it. Both frontends now reuse the existing `heap_calloc_closure_env` and its
`GC_TYPE_ENVIRONMENT`/paired-slot layout. The latter already traces initialized
Items independently of Function publication. `p2_traced_capture_env`, SHA256
`fe052180e40842d8c1cae1c9a7e108a2c61a0c7be672cf525b46c2e3735b9f26`,
passes the affected comprehension and capture fixture in all **18/18** tier/GC
combinations. No GC scanning model or value layout was added.

Full's dispatcher route and T0 now normalize static named aggregate arguments
against their formals while retaining the pre-evaluated receiver. Dynamic
named calls remain rejected (**S10.1.2v4, S12.3.2**). Optional holes retain the
existing missing-argument marker; variadic source counts remain visible in
Full. `p2_named_pipe_defaults` is building; its expanded default/var-pipe fixture
and complete output/GC gates are pending.

The benchmark fingerprint now includes `collection_runtime.cpp` (48 files).
New runner manifests resolve those registered paths against each frozen source
snapshot. Historical 47-file manifests remain unchanged; do not overwrite
older provenance. No current performance campaign is running, and no commit or
completion is claimed.

### 10.22 Named transport milestone and procedure admission

`p2_named_borrow_transport`, SHA256
`494c794d68d7a7ef658d88fafc78afde8227c3a9a8af2e2d6fdc6ed0f6252bee`,
closes the focused named/default/var-pipe and capture GC matrix: **36/36**
tier/GC combinations and **22/22** real pipe/closure/named fixtures. Both
compilers retain **77/77 untyped and typed** benchmark correctness. The
five-method command still has six Full control failures; it is not a clean
aggregate pass. Private native image/cache checks pass **8/8** in each GC
mode. Native security tests pass **85/96**, including all 48 interpreter
controls; 11 JIT/MVP rows fail with task-creation or builtin admission
rejections. Their oracles and compiler-selection assertions are unchanged.

A source-frozen screen for this engine is queued behind other tasks' tests;
`p2-named-borrow-transport-screen-supervisor.log` and its serial typed/untyped
reports retain the evidence. The monitor pauses for builds and test
supervisors. No performance target is closed by these correctness results.

The next slice admits registered fixed synchronous native procedures through
MVP's existing ABI/root/result emitter. Effect colour alone is not an ABI or
an ownership proof (**D7.1.2–D7.1.3**). Async procedures retain their current
admission boundary. `set` requires explicit place lowering: root owners use
the shared COW setter; projected owners detach and publish their spine before
mutating the installed private leaf (**S9.1.3v2, S9.2.2**). It invalidates any
inferred element-domain preservation proof. This source is not yet validated.

### 10.23 Fixed native procedures and VMap place ownership

`p2_native_procedures`, SHA256
`44139ba58b26cd40ad8b495dba9c9f2463df24a447f42c7a50ebe2f813439d3e`,
admits fixed synchronous native procedures and place-aware `set`. Its four
fixture matrix passes all MVP rows but exposes missing insertion capture in
Full and a stale T0 projected-set admission guard. The native security suite
improves to **86/96**; ten JIT failures remain and no oracle is weakened.

The common VMap hashmap insertion now share-marks retained child values.
Full stages the setter operands once and uses its existing insertion-capture
helper to update both runtime sharing and compiler ownership facts. T0's
scan now accepts the projected local-root shape already handled by its
place-borrow evaluator. `p2_vmap_capture`, SHA256
`a1d5664c65b30a391b05f989d84ff9e17c80c7d1aca7664da45f5088e0854794`,
passes **36/36** tier/GC combinations over the new `mir_mvp_vmap_places`,
existing VMap, projected mutator and clock-precision fixtures. The oracle
retains nested/root snapshots, cold float payloads, rejected keys and list
insertion captures (**S9.1.3v2, S9.2.2, S9.3.1**).

Audit confirms parking needs no MIR state-machine transform: a runtime
activation retains its native stack and precise roots. Fixed native
procedures therefore use the same registry-driven emitter whether or not
they may park (**S13.1.2v2, D7.5.3**). `p2_fixed_native_abi`, SHA256
`c0c899d82ca38620d3e6c9435e4cf48de596a6c36fcbda349f26615bb4d31c6a`,
passes all MVP and T0 rows in the next four-fixture matrix, including direct
and indirect parking calls; the matrix is **33/36**, because Full crashes
in all three modes of the new parking fixture. The frozen starting Full
also exits 139 on its unchanged source/oracle. The retained MIR dump shows
an escaping untyped parameter inferred as ArrayNum from a visible direct
edge; its boxed entry strips an integer tag and asserts the array witness.
Array-only inference now requires non-escaping, non-boxed callers; without
an array-specific guard/slow body, an open caller supplies no representation
proof (**D4.3.1–D4.3.3, D7.2.2**). `p2_closed_array_entry` is building to
validate that fix. Task creation/structured scopes remain unadmitted.

The native fixed-ABI security suite and full typed/untyped correctness checks
are in progress. New benchmark fingerprints include `vmap.cpp` (49 files),
resolved against each candidate's frozen 3,116-file source inventory. Existing
48-file timing provenance remains unchanged. No performance or aggregate
Lambda GTest gate is complete.

### 10.24 Native admission audit and wildcard member steps

`p2_closed_array_entry`, SHA256
`5f5e12bd1047489f422d74a7c3a1b13c21119d8fbf7a2017896f4781bbc1cb6f`,
passes **36/36** parking/map tier/GC checks and **7/7** real-harness fixtures.
Both compilers pass every typed and untyped benchmark output. The focused
five-method command passes all 84 MVP content/GC rows and retains six older
Full failures, exactly the procedure-result/closed-array controls. Native
cache checks pass **8/8 normal and forced GC**. No timing is claimed from
these correctness commands.

The fixed-ABI security run initially reports **89/96**, but the scoped
SQLite test accepted an `AST_NODE_OPEN_STAM` compiler rejection as a runtime
error. Its oracle only asserted Error, allowing a stale error payload to
hide the missing runtime execution. Inline security tests now retain the
loaded Script and require its MVP main entry, with no interpreter routing.
The source-matched rerun is **88/96**; the additional failure is truthful
coverage debt. This check changes no production permission policy and skips
neither calls nor oracles. File-load refusals retain their existing checks.

Two other failures are real wildcard-step lowering omissions. An isolated
four-entry directory yields `0/0` in MVP and `4/4` in Full for `.**#` and
iteration over `.**`. The MIR dump shows MVP calling `fn_member_by_id` for
`**`, treating the step as a named field. Both frontends now use one shared
`em_wildcard_step` for the existing `fn_step_items`/`fn_step_closure` kernels
(**S8.2.5, S2.4.6–S2.4.7**); MVP plans their collecting/reentry effects.
`p2_wildcard_steps` is building. Validate the existing wildcard/run/path
fixtures and native enumeration/budget tests before claiming this route.
Structured task scopes, resources/open and all aggregate gates remain open.

### 10.25 Wildcard and persistent-slot validation

`p2_wildcard_steps`, SHA256
`6b750641501d515f5f60e3b69011a4073a88261da5f9d54de67cfb9982a8c45b`,
passes both native enumeration/budget cases under interpreter/JIT in normal
and every-allocation GC (**4/4 each**). The ordinary wildcard family is
**3/5**. The five-fixture tier/GC matrix is **28/45**, retaining every
failure. Quoted member names crash MVP because it casts the literal as an
identifier. A list-valued map field also keeps its spread bit and is spliced
by a following member step. Both are implementation defects, not oracle
changes (**S8.2.5–S8.2.6, S2.5.6**).

Member lowering now handles quoted keys as values through `fn_member`.
Full's existing list guard/slot-image emitter is promoted to the common MIR
header; both frontends use it for persistent field/attribute slots. Its
original fast guard still calls `slot_image` only on an actual list.
`p2_member_slot_images` failed to build because its promoted guard referred
to Full's private offset constant; v2 uses `offsetof(Container, flags)` but
revealed the now-unused old constant. v3 removes that constant; failed
source snapshots and logs remain separate.

`p2_member_slot_images_v3`, SHA256
`dfc773e97b6219aef4d70a89147937abcb8e153b500efb269a12fe6cbbb52004`,
passes **9/9** real wildcard/list/navigation fixtures and **36/36** tier/GC
combinations over wildcard steps, run stepping, list landing and MVP element
construction. All **77 untyped** outputs pass in both compilers. The native
security suite, including explicit inline-script admission, passes **90/96**:
only five task-creation cases and scoped SQLite/open remain. It is not an
aggregate Lambda baseline pass.

The broader five-method check has **eight failures**: the six older Full
controls plus new MVP typed List/Havlak compile rejections. Slot imaging had
widened even non-list map domains to `any`; emission added an admission/error
edge absent from the plan. The revised proof preserves non-list domains and
plain array contracts through the imaging operation, while list-specific
predicates still require admission (**D4.3.1–D4.3.3, S2.5.6**).
`p2_slot_image_domains` is building. Do not claim typed preservation until its
complete output and ownership checks pass.

The path wildcard/query fixtures' forced-GC corruption is shared pre-existing
debt: starting Full, `p2_closed_array_entry` and `p2_wildcard_steps` all fail
both unchanged oracles in every-allocation GC. Source/oracle/binary hashes
and outputs are retained in `p2-wildcard-path-full-gc-controls.json`. These
controls establish prior failures, not a root-cause fix. Normal path fixtures
pass the real harness. No oracle, GC knob or timeout was weakened.

### 10.26 Slot-image domain proof and joined roots

`p2_slot_image_domains`, SHA256
`f98f4229c62e7b62099377a5f9fec77f8b4dd22a9764896e73698fac39cafec5`,
restores all **77 typed** outputs in each compiler and all **84 MVP
content/GC combinations**. Its four-fixture wildcard/list/element matrix is
**36/36**. The five-method command retains exactly the six older Full
controls; the typed List/Havlak admission regression is fixed. All **77 untyped** outputs also pass under each compiler; both sweeps are complete.

The new shared image helper returns a joined Item register. Code review
found that preserving an input's domain also retained its metadata without
registering this new physical owner as a root candidate; the allocating
arm's register alone does not cover later field evaluation or map allocation.
The helper now registers its joined result through the existing frontend
root hook (**D6.3.1–D6.3.3**). `p2_slot_image_roots` is building to validate
this ownership fix. Its gates remain open; no failed oracle is removed.

Task-scope implementation audit for the next slice: promote Full's indexed
launch classifier rather than copying it; task-target references must escape
and the syntactic argument array is not a direct-call edge. Reuse the task
argument normalization/launch ABI through a shared MIR helper. Follow the
shared `owns_task_scopes` fact: enter block scopes, preserve the entry's task
base, join normal block/loop exits and cancel error exits, including deferred
block failures. The common MVP epilogue is before precise root/number pops;
its stack-overflow cold exit returns directly without executing that epilogue.
Keep scoped definitions out of inlining/tail-call elision until their cleanup
edges are implemented. Preserve `start`'s capture restrictions, escape and
document ownership (**S13.1.1v3–S13.1.4, S13.3.1v2, D7.5.1–D7.5.3**).

The first joined-root snapshot, `p2_slot_image_roots` (SHA256
`6f29b3f9a2e69f0949ea6a43b62fd57d6a341e610addaabff8e8704b8eb47e91`),
built successfully. Review moved the root hook after the initial value move:
Full's eager root oracle must never read an uninitialized joined register.
`p2_slot_image_roots_v2` is building with that instruction order; qualify v2
rather than the preceding snapshot. Source-frozen timing remains at 74/77
untyped rows and is paused for ongoing builds/tests, with typed still queued.

### 10.27 Validated rooted slot-image milestone

`p2_slot_image_roots_v2`, SHA256
`cb578043050a4e1db94f5f1f818876b5771342c4fbae2808cf1e5229c5676d72`,
passes **77/77 typed and untyped** outputs in each compiler, **36/36** focused
wildcard/list/element tier/GC combinations and **11/11** real-harness
fixtures. The five-method command passes every MVP row (84 content/GC,
12 callable/GC, 16 window compiler/seed controls and 3 open-var modes); it
retains the six older Full procedure-result/closed-array failures. The two
typed admission regressions from v3 are fixed, and no golden changed.

Fresh source-matched native executables pass the eight selected cache cases
in normal and every-allocation GC. The complete security suite is **90/96**:
all 48 interpreter controls and 42/48 JIT/MVP controls pass. Five task-launch
cases and scoped SQLite/open remain explicitly failed. Forced-GC recursive
walk and entry-budget controls pass **4/4**. Root executables remain untouched.
The native build manifest retains the test/header snapshots and object owners.

The monitored untyped screen on the older frozen
`p2_named_borrow_transport` is complete: **77 valid rows, 26 lower medians,
24 one-sided screening bounds below Full**, geometric mean MVP/Full
**1.19133695598**, and **13 discarded disturbed whole rounds**. Each case has
three retained pairs and one warmup; this does not qualify performance.
`p2-named-borrow-transport-untyped-screen-summary.json` retains all 53 rows
that do not clear that interval gate. The serial typed screen is still
running; retain its flags before any new timing campaign. The current rooted
slot-image engine is not covered by those timings.

Release/source manifests, corpus v2 hashes and raw logs remain under the
artifact root. The current runner fingerprint has 49 files; historical
48-file timing provenance is unchanged. `git diff --check` and fixture/golden
pairing checks pass. No commit or completion is claimed; the goal is active.

### 10.28 Task/resource cleanup implementation refinement

The next slice should use a lexical cleanup stack in MVP rather than add
independent copies of task/open unwinding at every exit. Each record keeps
its parent, kind and task scope register; an open record closes through the
existing `fn_open_end` ABI. `MvpControl` and `MvpErrorTarget` retain their
cleanup destination. Return/error, loop transfer and deferred-block failure
emit only the records crossed by that edge, leaving compiler stack state
unchanged for other generated branches. The normal statement path closes
its own record before the existing failure join; the error path has already
unwound it and must not close it twice. An inlined activation starts a fresh
compiler cleanup chain and restores its caller's chain, so a callee error
cannot unwind the caller. Keep resource/scoped entries out of inlining until
that isolation is tested. This refines §10.26's epilogue sketch; the cold
stack-overflow return still bypasses unentered-body cleanup.

Use Full's `analysis->owns_task_scopes` syntactic fact to avoid adding runtime
scope calls to ordinary benchmark functions. Launch targets need boxed
entries, and the indexed launch classifier must be shared with Full: its
syntactic argument array is not a normal direct call. Preserve packed-array
argument normalization, escaped/document-owned tasks and frontend capture
checks. `lambda_task_scope_current` can lazily create the root task through
`lambda_current_task`; retain conservative call effects rather than assuming
it is allocation-free. Open aliases need a planned local register despite
having no ordinary declarator. Audit handler/fault cleanup before admitting
resource bodies (**S13.1.1v3–S13.1.4, S13.3.1v2, S12.4.4v4, D7.5.1–D7.5.3**).
No task/resource lowering code has been added in this milestone.

### 10.29 Completed monitored typed screen

The serial screen on frozen `p2_named_borrow_transport` finishes with all
**77 typed outputs valid**, **15 possible-regression flags**, one
resolution-limited case (Sumfp), and two discarded disturbed whole rounds.
`p2-named-borrow-transport-v2-typed-screen-summary.json` retains every flag
and its interval. The append-only confirmation selection v6 contains **49
rows**, retaining all 44 prior rows and adding Ack, Matmul, Brainfuck,
JetStream Richards and Prettier. Primary 30-pair/two-warmup and preselected
50-pair confirmation campaigns remain required; use matched batched Sumfp.
Do not treat this three-pair result as typed preservation, or apply it to
newer source. The screen supervisor exited successfully; no campaign is
running. No C2MIR or Radiant baseline was run by this goal.

### 10.30 Task launch and lexical task cleanup in progress

MVP now lowers same-context `start` through a shared MIR task-value helper
used by Full. It retains packed-argument normalization, boxed function
entries, escape/document ownership and frontend capture checks. Full's
indexed launch classifier is promoted rather than copied. A task target is
an escaping value; its syntactic vector is not a direct parameter edge
(**S13.1.1v3–S13.1.4, D7.2.2**).

MVP uses a lexical cleanup chain. Statement scopes, loop destinations and
block-failure joins record their base; success joins children, error exits
cancel then join, and loop transfers close only crossed scopes. Inlined
activations isolate the compiler chain; task-owning entries are currently
excluded from expansion and tail-call elision. Runtime scope calls are added
only for the shared `owns_task_scopes` fact, so ordinary benchmark functions
retain their path (**S13.3.1v2, D7.5.1–D7.5.3**).

`p2_task_scopes`, SHA256
`ef34e0bf48b0d94d9344381cf0cd6721768b26731b729c116522c3a20c672853`,
passes **69/72** focused tier/GC combinations: the new task-value fixture
(packed vectors, cold floats, named/dynamic targets, readonly captures and
argument snapshots), scope join/return/loop/escape, no replay and cancellation.
Only `start_wait` crashes in all three MVP modes. Its zero-argument worker
has an explicit argument array/options; the remaining ordinary-call effects
loop dereferences nonexistent formals. Launch nodes already carry their own
conservative effects, so the inner syntax is now excluded from that loop.
No timeout or oracle is changed.

The next snapshot also prepares public imported function references through
the same existing planner factory for direct calls, callable values and task
targets. Scope-enter/current result descriptors now identify their native
scope records as non-GC pointers while retaining conservative effects;
`lambda_current_task` may create a root task. New paired
`conc/start_imported` tests the direct and stored imported target forms.
`p2_task_call_edges` is building; its full gates are pending. The first
source-matched native task security suite is running on `p2_task_scopes`.
Resource/open cleanup, handler/fault exits and broader concurrency remain
open. Root binaries remain untouched and no new benchmark campaign runs.

### 10.31 Task call-edge milestone and broad native controls

`p2_task_call_edges`, SHA256
`01c9ec854024dd0762ef8efe7bccc50c1a0364d411f24c95720b745441da588d`,
passes **108/108** tier/GC combinations over twelve task/cancellation/scope
fixtures and **13/13** real-harness fixtures. Explicit empty vectors/options
and public imported targets now pass, including stored imported callable
values. The durable Python `test_scoped_tasks` retains the twelve fixtures;
its GC/compiler runner is shared with the existing content/callable methods,
with their original populations and oracles preserved.

Both compilers retain **77/77 typed and 77/77 untyped** corpus-v2 outputs.
The six-method Python command passes all MVP subtests, including the 72
compiler/GC scoped-task rows, and retains only the six older Full controls.
Source-matched native security is **95/96**: all interpreter controls and
47/48 JIT/MVP controls pass. Scoped SQLite/open is the sole remaining native
security failure. No performance measurement on this newer source is claimed.

The complete private native concurrency executable is also run under both
compiler policies. Both runs have exactly the same **45 failed tests** plus
one failed suite setup (the JSON contains 46 failure records). No MVP-only or
Full-only failure occurs. The first failure is DOM member ordinal parity;
subsequent logs show a current evaluation context left bound and refusing
fresh runtime owners. This is unresolved broad-gate evidence, not a clean
concurrency pass. Raw outputs, JSONs and paired failure sets remain in
`p2-task-call-edges-native-concurrency-control-summary.json`. Investigate the
ordinal/provider and context ownership causes before aggregate qualification;
do not weaken those assertions or count this as MVP-specific language debt.

Resource/open work can reuse the lexical cleanup chain. The open alias is
already an indexed ordinary declarator (`alias_decl`), with no source
initializer; bind the successful `fn_open_begin` result instead of evaluating
that empty initializer. Protect its produced domain from initializer-based
narrowing. Share Full's CRUD emission with frontend key/error callbacks.
Normal exits commit, error exits roll back, and a failing commit must abort
remaining outer records without closing the completed record twice. Preserve
Full's explicit Error-return rollback inside an open block, task joins before
commit and handler/fault ownership (**S12.4.4v4, S13.3.1v2, D6.3.3**).
No resource/open lowering is included yet.

Fresh-process selected cache checks on `p2_task_call_edges` also pass **8/8
normal and 8/8 forced GC**. This confirms the focused cache behavior but does
not clear the broad concurrency suite's contamination/ordinal failures.

### 10.32 Transaction scopes and shared CRUD emission

MVP now supports `open` and CRUD through the runtime's existing data model.
Full and MVP share CRUD emission with frontend key/result callbacks; no edit
ABI or interpreter fallback is added. An open alias uses its indexed ordinary
declarator but binds the successful runtime producer with an open Item domain.
The lexical cleanup chain commits clean exits, rolls back error exits and
removes a failed commit before unwinding ancestors. Return admission precedes
commit; an explicit Error return rolls back even when admitted by the return
contract (**S12.4.4v4, D6.3.3**).

The initial `p3_open_cleanup` snapshot fails compilation because a scripted
refactor expected two boxed setter guards but found three. The corrected
`p3_open_cleanup_v2`, SHA256
`7f66937790d6e04628d1cf2a3cbffd144f1cfdd5e812aa126da01afb19e8e88c`,
passes native security **96/96**, including actual MVP admission for scoped
SQLite, but only **24/27** focused tier/GC rows. The unchanged inner-block
store-failure fixture exposes procedure-wide rollback: the shared guard must
use the local block failure join, while explicit returns keep their callable
exit (**S7.7.6v2**). No oracle is changed.

`p3_open_containment`, SHA256
`b871ced3a8b6c71188a542f42a7c27a9aaa7f9bb529aa5044ed7bc4ef7dd8e98`,
fixes that boundary and passes **27/27** tier/GC rows, **3/3** real-harness
fixtures and source-matched native security **96/96**. Its durable Python
open/task/typed-benchmark command passes all three methods; MVP and Full each
retain **77/77 untyped** and **77/77 typed** outputs. Evidence is under
`p3-open-containment-*`. Runner fingerprints now include the write-set header
and implementation; prior manifests remain unchanged.

### 10.33 Procedural local-fault handlers and fresh census

On the open-containment engine, the existing local-system-fault and
fault-after-wait fixtures fail MVP in all three GC modes; Full and T0 pass.
The equality-depth fixture already passes. MVP ordinary recovery had no native
recovery frame. Promote Full's physical frame/checkpoint/landing emission to
`em_local_fault_expression`, used by both compilers. The JIT activation calls
the platform checkpoint directly; the landing restores precise watermarks,
publishes the durable fault and cancels abandoned child scopes before recovery.
MVP lexical cleanup also retires a protected frame on explicit control exits.
The recovery-frame native pointer has an explicit non-GC result descriptor;
conservative call effects remain (**S7.6.1v9, S7.6.7v5, S7.11.1v2, S7.11.4,
D7.5.3**).

`p3_local_fault_handlers`, SHA256
`838ef37f401ebc553aa2b48076da82c7201d9f5dd778a0c3ed44eaa2f901cb30`,
passes **36/36** tier/GC rows over system/equality faults, fault after parking
and handler results, plus **4/4** real-harness fixtures. The durable Python
fault/open/task/typed-benchmark command passes all four methods; both compilers
retain **77/77 typed** and **77/77 untyped** outputs. No performance result is
inferred from these correctness runs. Runner fingerprints now include the
recovery-frame sources, with historical identities unchanged.

The fresh MVP discovered-script census runs against this release and a private
fixture snapshot. `p3-local-fault-handlers-census-inputs.json` hashes **8,043**
inputs; the private Lambda tree is frozen and benchmark inputs use corpus v2.
Its log/JSON use the `p3-local-fault-handlers-discovered-census` prefix. Results
and Full controls are pending; this is not aggregate GTest qualification.

### 10.34 Private module storage and numeric literal expansion

The fresh census identifies private composite globals such as record tables
and nested arrays that have no storage descriptor. A module with public
functions can have no top-level call or `pub let`; the old `context_globals`
test therefore misses those lexical reads. Plan global storage before call
and effect analysis. Cross-function globals that cannot use the existing
immutable native-scalar or primitive literal-table paths use the same
per-evaluator slab as exported values. The literal-array proof is shared
between planning and emission rather than copied; preserve declared admission
proofs for native scalar slots (**D6.3.3, S9.1.2**).

The paired `_mir_mvp_module_values` / `mir_mvp_module_values` fixtures exercise
private record tables, nested numeric arrays and computed text. The first
probe is invalid because its helper name `number` is reserved; rename that
new helper to `nested_value`, retaining the earlier report. The corrected
fixture passes MVP in all three GC modes. Its unchanged oracle exposes a
Full container-store refusal and T0 mutation of the provider's aliased table;
these controls remain failures, not an accepted semantics difference.

`p4_module_wide_literals` also admits i64/u64 contracts through boxed runtime
arithmetic and lowers their literals through the existing number-home ABI.
Complex literals use `complex_new`. Nullable exact-width values retain Item
transport; no data model or numeric semantics changes (**D2.3.4–D2.3.5,
D6.2.3**). The initial eight-fixture matrix is **60/72**: all Full/T0 numeric
controls pass, while MVP wide arithmetic and nested linear-science operations
fail, and the module mutation controls fail as above. No failing oracle is
changed.

### 10.35 Scalar-home result liveness and handler result domains

Wide arithmetic exposes a common emitter lifetime defect. The frozen
`p4_module_wide_literals` minimal trace prints `[3, 3, 6, 0, 9]` for two
independent i64 bindings and their operations. Every manually owned scalar
home is colored at byte offset zero. Home addresses have fixups, but the
returned tagged Items do not identify their payload home to the liveness
pass. Bind materialized local-home addresses and propagate that binding through
shared float boxing and Item adoption. The payload remains live through its
last observer; no global disabling of coloring or conservative scanning is
added (**D6.2.3, D5.4.4**).

`p4_owned_home_liveness`, SHA256
`f1d4276f919d1ee3022cc9b4313a36d34efc632c32eb2d1d112507d5b3ce15dc`,
produces the correct independent values, with distinct live home offsets.
Its partial matrix has **39/45** completed rows before the driver encounters
an existing MIR-structural fixture without a text golden. The six failures
are MVP's three nested-linear-science rows and the older three Full procedure
controls. This partial driver exit is retained, not reported as a passed gate.
The existing wide-arithmetic fixture now passes all tiers/GC modes.

Plan conservative effects for procedural native fault-frame setup.
`admission_block_outcomes` additionally exposes an unrelated unnecessary
admission helper after `or^`: a consumed null/error cannot survive its
pass-through arm. Join that remaining domain with the recovery body's actual
contract, keeping fresh recovery errors and filtered misses. This removes the
unplanned collecting validation call without weakening its destination
(**S7.6.1v9, S7.6.8, S11.4.1v5**).

The current `p4_handler_domains`, SHA256
`74e409e21a13351336869c90f0f9929578af2c9c2464ccae4c3794d67ff4472d`,
passes the durable five-method numeric/fault/open/task/typed-benchmark command
(**304 script checks**) and retains **77/77 untyped** under each compiler.
Both compilers also retain **77/77 typed** outputs. The selected real fixture
family is **12/14**; only nested linear-science and constrained-handler support
remain. The handler tier/GC matrix is **42/45**, with three MVP constrained-type
refusals. The current module matrix retains **3/3 MVP passes** and **six
Full/T0 failures** described above. Logs/JSONs use `p4-handler-domains-*`.
No timing comparison or typed performance preservation is claimed.

### 10.36 Completed MVP discovered-script census; Full control pending

The frozen `p3_local_fault_handlers` census completes **950/2,086** discovered
scripts, with **1,136 failures** and zero drift across its 8,043 hashed inputs.
Its summary retains every failed test and all admission reasons. Frequent
shared causes include unsupported native-module builtin dispatch, private
module composite storage, native Boolean result joins, object/view routes,
parameter contracts and constrained types. Repeated reason counts include
transitive import diagnostics and must not be called unique-test counts.
P4 changes do not retroactively qualify this older snapshot.

The Full JIT control uses the same executable and private corpus. It is still
running in exec session **59932**, with the
`p3-local-fault-handlers-full-discovered-control` log/JSON prefix. Pair failure
sets only after it finishes, and retain timeouts/missing batch results. The
older broad native ordinal/context failures remain separate. No aggregate
Lambda baseline or performance gate is declared green.

### 10.37 Retained call-result ownership (2026-11-06)

The module-value oracle exposes a shared ownership proof defect: every call
result was classified as a fresh owner, including a returned global or
captured value. Full consequently selected a raw child-map setter for an
Input-owned static map; T0 changed the provider's retained map. This is an
implementation error under **S9.1.2**, not an oracle or language change.

Reuse the existing returned-parameter alias analysis with a retained-scope
query. A function-owned `FnAnalysis` fact propagates through later-defined and
recursive callees; globals and outer captures retain an observer. Opaque
callables have no freshness proof. Shared producer classification, Full's
store planning and T0's call/binding capture now read that fact. Argument-root
marks still follow their separate parameter-alias facts. Successful postfix
propagation preserves the source owner (**S7.6.3v3, S9.1.2, D8.2.4**).

`p4_retained_call_results`, SHA256
`d8002c59221506fd1ea79faa5f7ff9c380828e324e65a2f33d71f7003303f39d`,
passes the initial direct/forwarded/stored-callable module oracle in all nine
tier/GC combinations, plus both compilers' **77/77 typed and untyped** outputs
and the then-current six-method Python command. The broader matrix is
**42/45**, retaining only the older three Full `mir_mvp_closed_arrays` failures.

`p4_retained_propagation`, SHA256
`32bd5468826584c0dab4235b5f6749a800bc69f261f4ce3935429bfaa671e1d2`,
also covers propagation and retained closure storage. The expanded oracle has
twelve true results and passes **9/9** tier/GC combinations. The first expanded
probe/combined Python command is invalid due to a missing semicolon before a
leading lambda in the new helper; preserve those red logs. After correcting
that test syntax, the module method passes its six compiler/GC rows. The five
other methods passed in the combined run, including every typed benchmark;
both separate untyped sweeps retain **77/77**. Four selected real fixtures
pass. Source-matched native security is **96/96**, and the eight selected
cache tests pass under both normal and forced GC. Broad native concurrency
is not qualified by these focused results.

`p4-retained-propagation-module-controls-v3-dependencies.json` records the
main source/golden plus the helper source/golden and verifies every dependency
hash after the run. The temporary matrix runner now supports explicit shared
input dependencies. No old report is rewritten to claim the expanded fixture.

### 10.38 Shared range-member admission regression and new census

The completed `p3_local_fault_handlers` paired census has **950/2,086** MVP
passes and **1,693/2,086** Full passes: **757 MVP-only**, **14 Full-only** and
**379 shared** failures. Test input hashes are stable, but those manifests
cover `test/`, not all linked package sources. Repeated import diagnostics are
not unique-test counts. Many Full errors cascade from timezone package
admission, so investigate the shared compiler before assigning them to
pre-existing debt.

A controlled AST build on identical current package scripts accepts
`chart/timezone.ls` with the fresh starting release and rejects it with the
then-current release. Binary bisect accepts `p1_query_datetime` and rejects
`p1_range_frontend`. Package script hashes remain stable. The earlier
character-range fix changed the unknown member type from an unsafe `int`
guess to `any`, accidentally admitting Error into the iterator domain.
Calls consuming those members then require error handling despite the range
never producing Error members. This is a shared frontend regression introduced
by this goal, not a stale package port.

Keep construction failure separate from member admission. A successful range
contains `int | string`; use that clean union for unresolved bounds. This
preserves codepoint strings and clean call admission, while source construction
errors still propagate (**S11.1.3, S7.7.3, S7.9.3**). No package source, oracle
or diagnostic is weakened. The paired `mir_mvp_range_domains` fixture covers
numeric/character dynamic bounds consumed by another clean function and
explicit propagation of a failed range construction.

`p4_range_member_domains`, SHA256
`0f8f7ecc08e26ae67778cc9ca76c73fd5452b7b5935b0c6b33983e8f9cc1104c`,
restores timezone-package AST admission. Its range/query/module matrix passes
**27/27**, and all seven durable focused Python methods pass **322 script
checks**. MVP and Full retain **77/77 typed** and **77/77 untyped** corpus-v2
outputs. Existing constrained-handler, matrix and other language gaps remain.
No current performance qualification is inferred.

A new paired census runs this release with separate compiler workspaces and
two batch workers per policy. Its private snapshot copies the current Lambda
fixtures and `lmd`, retains corpus-v2 benchmarks, and hashes other linked test
inputs. `p4-range-member-domains-census-inputs.json` contains **8,989** files,
including **940 lmd files**. Both policies discover **2,089** scripts. Exec
sessions are **2946** (MVP) and **96780** (Full). Results are pending; do not
apply corrected-source results to the old census or declare baseline success.

### 10.39 Constrained-type implementation audit; code pending

The next shared feature slice is constrained-type membership/match/handler
patterns. **S11.4.6v4** explicitly enforces the base only at ordinary annotated
boundaries; first-class generic `fn_is` also tests the base only. Named/inline
`is` and match patterns execute every predicate layer, innermost first
(**S11.1.3, S11.2.1, S11.4.11**). Preserve this distinction.

Full's `emit_constrained_type_test` provides the shared base/predicate/truth
shape to promote with frontend predicate/truth callbacks. MVP needs its
synthetic `that` functions to use canonical boxed exports; their name means
`mvp_function_expression` currently excludes them. Bind their candidate
parameter as current item/root at entry, with null index/parent. Reuse the
existing imported-function planner factory with an explicit declaring owner
for imported predicate exports, then use the existing hidden-context/env ABI.
Do not copy Full's physical emission or introduce an interpreter fallback.
Predicate errors/falsy results fail membership; recovery filters and chained
layers use the same test. Plan their GC/reentry effects. Allow base-only
annotated constrained contracts through the existing validator.

Local constrained types may capture type bindings, which the current readonly
capture admission rejects. Audit those compile-time values in the shared
capture reader and binding setup before admitting recursive/outer-local cases.
Retain explicit refusal until their ownership/ABI is implemented; generic
runtime membership cannot substitute for executing named predicates. No
constrained-type lowering is added in this checkpoint.


### 10.40 Native constrained predicates and current release graph (2026-11-06)

**S11.2.1/S11.4.11** require named/inline constrained membership and match
patterns to admit the base, then execute each predicate layer innermost first
in its declaring scope. A falsy or Error answer fails the test.
**S11.4.6v4** keeps ordinary annotations and first-class generic membership
base-only. No semantic ruling changes here.

Promoted Full's physical base/predicate/truth loop and imported boxed-entry
trampoline into `mir_function_value.hpp`; both MIR implementations call these
helpers. MVP plans synthetic `that` functions as canonical boxed exports,
uses the existing imported-function factory with an explicit declaring owner,
and records implicit predicate GC/reentry edges. At entry, the candidate is
current item/root and parent/key are null. Immutable captured type bindings
remain type values and occupy their original environment slots without being
mistaken for mutable lexical homes. Local calls discharge the result companion
before truth classification. There is no new interpreter fallback.

HEAD advanced to `b8e29b077` while the goal was inactive. The old private
release graph omitted three newly merged units (`io_rdb.cpp`,
`io_rdb_value.cpp`, `rdb_spool.c`), and its source inventory omitted macro
files such as `dom_api.def`. The first private compile therefore linked with
undefined RDB providers. It is retained as **compilation-only**, never as a
successful tested binary. Extended the private graph to match the current
`lambda-rt`/`lambda-lib` source patterns and froze omitted tracked source,
header and macro inputs. The qualified graph has 769 units and 3,543 input
files; the source inventory check reports zero drift. Build root binaries and
vendor sources remain untouched.

Evidence uses `p5-constrained-current-*` for the initial predicate-only release
and `p5-constrained-objects-*` for the final slice. The initial MVP matrix is
15/21: five predicate fixtures pass all three GC modes; two are refused at
nominal declarations. The final release fixes those declarations/literals:
all seven constrained fixtures pass all three tiers and GC modes (**63/63**).
The seven fixtures cover inline/named/aliased tests, recursion to depth 2,000,
outer locals and nested closures, imported private helpers/module bindings,
Error answers, annotation bases, and filtered functional/procedural handlers.
`test_constrained_type_predicates` keeps these cases in the durable Python gate.

### 10.41 Checked nominal literal values (2026-11-06)

MVP now materializes immutable nominal type descriptors and constructs nominal
maps/elements through the existing `object_from_fields` runtime boundary.
That boundary resolves optional presence and admits all fields before exposing
the packed layout (**S16.9.5/S11.4.10**). Declared defaults are evaluated only
for absent keys. Written and spread fields use the existing keyed builder and
persistent slot images; content uses `object_content_fill`, preserving element
normalization (**S2.1.3/D3.3.1**). A shared literal-staging helper replaces the
repeated map/element staging setup before adding the object case.

`mir_mvp_nominal_values.ls/.txt` pins spread independence, default values,
optional absence, nominal/structural identity and normalized content. Together
with `object_default`, `object_constraint` and `object_field_admission`, all
three tiers/GC modes pass **36/36**. Object-level constraint behavior remains
Full's existing base-only behavior; method-bearing types are explicitly
refused until their registration/receiver ABI is implemented. The unchanged
`object_boxed_field_layout` fixture exposes this remaining refusal in three
MVP rows; its six Full/T0 controls pass. Do not report that 81-row diagnostic
matrix as all green.

Final release SHA256:
`032973621496469fb4343e7e78d82a9abd6777a8a822d9260e6930ab2a8c7555`.
Nine focused Python methods pass **388 script invocations**, including 77/77
typed goldens under each MIR policy. Separate frozen-v2 untyped runs retain
**77/77** under each policy. The real private discovered-script harness passes
**11/11** selected tests with `LAMBDA_EXEC_BACKEND=jit` and
`LAMBDA_TEST_MIR_COMPILER=mvp`. A negative-test filter matches zero discoveries;
that command is retained as zero executed tests, not negative-test coverage.
No benchmark timing or aggregate Lambda baseline result is claimed.

Source-matched private native executables also pass security **98/98** and
the selected cache lane **8/8 normal + 8/8 forced GC**. The two view cache
cases remain outside this focused lane, as before; the full native concurrency
failure set remains open. Native logs, JSON and executable/object hashes are
recorded under `p5-constrained-objects-native-*` and
`p5_constrained_objects-native-tests-admission/manifest.json`.

### 10.42 Range census completion and merge-input limitations (2026-11-06)

The older range-repaired Full census completes **1,731/2,089**, with 358
failures. Its MVP pair records 1,048 subprocess completions and then stalls;
a process check finds the harness alone, with no child processes. Terminated
only that task's stalled harness (PID 23336), retaining its partial log and
exit 143. No complete MVP count or paired failure partition exists for it.

The 8,989-input drift check detects **ten** changed linked demo/UI inputs
following the merge; the copied Lambda/lmd fixtures remain frozen. Those
linked-input changes and the older executable mean this census does not
qualify the current source tree. Do not attribute all shared failures to
pre-existing debt or combine it with a later result as one experiment.
The previous shared range-member regression is repaired as recorded in §10.38;
this new census evidence does not reverse that focused source-matched control.


### 10.43 Native nominal method entries and receiver snapshots (2026-11-06)

**S12.3.3v2/S12.3.4v2** define bound fn methods and dynamic invocation;
**S7.4.6v3/S12.1.2v3** govern early completion of either callable colour.
MVP now identifies a method's declaring nominal type from the immutable index,
exports its canonical boxed entry, and receives the receiver through the same
hidden argument as Full. Receiver fields become activation-local bindings;
current item/root are self, while parent/key are null. Procedure completion
publishes only potentially written fields at the common return point.
Optional presence survives untaken writes and explicit null assignments
(**S16.9.5**). Declaring scopes, field contracts and nominal identity remain
shared; no runtime data layout changes or new interpreter fallback are added.

Promoted Full's physical field load/presence test, field publication and method
registration into `mir_function_value.hpp`. The two frontends retain their
binding/representation choices but share these physical operations. MVP
prepares a procedure receiver before creating its mutating bound closure and
publishes the replacement to the caller. The initial method screen passes
15/33; remaining typed string-field loads were described as unrestricted
`any`, so the converter could not select a pointer lane. Restoring the
receiver's admitted field contract fixes that decoding root cause.

The new `mir_mvp_bound_methods.ls/.txt` pins a saved fn receiver before a later
pn mutation. The valid fixture initially returns `[false, false, true]` on all
three tiers. A separate control with the preceding `p5_constrained_objects`
release reproduces **0/6** under Full/T0, proving the shared failure predates
this method implementation. The first authoring attempt omitted mandatory
acknowledgement and was rejected E228; that invalid attempt is retained
separately, not counted as runtime evidence.

Root cause: the shared method binder retained self without recording a value
capture. It now roots self precisely and captures fn receivers by value;
procedure receivers preserve the caller's detached writable owner
(**S12.3.3v2/S9.3.1**). This fixes MVP/T0, but Full still skipped preparation
when its static `cow_marked` fact was absent. Runtime method binding can share
a receiver without changing that compiler fact. Full now consults the runtime
COW preparation path for every procedure-method receiver. All nine snapshot
rows subsequently pass. No conservative stack scanning is introduced.

### 10.44 Method defaults, raw spreads and nominal element writes

The 27-row expanded MVP screen passes 24/27; only named-method optional holes
remain. The frontend had already resolved/reordered these calls. Their omitted
argument nodes were lowered as explicit null, suppressing defaults or failing
a typed parameter. Both direct and dynamic MVP serialization now preserve
`ITEM_MISSING_ARGUMENT`; explicit null remains a supplied value
(**S12.3.1/S12.3.4v2**). The five-fixture tier/GC control then passes **45/45**.

A broader 22-fixture matrix reaches **192/198**. `object_update` crashes in MVP
while compiling a method's `<Vec *:~, ...>` body. LLDB and the AST dump show
that nominal updates store their spread as a raw expression, including
`AST_NODE_CURRENT_ITEM`. Shared literal staging incorrectly casts every item
to `AstNamedNode` before reading its key/flags. It now checks for
`AST_NODE_KEY_EXPR` before accessing that payload; raw expressions use the
existing spread lane. This also removes an accidental dependence on the
payload bytes of identifier spread nodes.

`proc/object_open_instance` exposes the other refusal: MVP admitted array/map
member-write carriers but omitted the element structural kind. Element-shaped
nominal instances use the same attribute-write boundary, so admission now
includes that actual carrier (**S2.1.4**). The existing builder preserves the
nominal record and content across shape growth. Both repairs retain the
original fixtures and goldens; the complete current matrix is **198/198**.

### 10.45 Current method milestone evidence and remaining work

Current release `p5_method_update_places-release.exe`, SHA256
`2fafec587161419615061f48e33da47f6e7bee7906e87e6166bc5e02cbc87fec`, is a private
O3 ThinLTO native build on HEAD `b8e29b077` plus the recorded uncommitted method,
constraint and nominal work. Artifacts use `p5-method-update-places-*`.
The 769-unit graph/3,543-file inventory from §10.40 remains; the native cache
fixture is independently frozen by its native-build overlay and manifest.

- Current tier/GC method matrix **198/198**, with the imported helper hashed.
- Ten durable Python methods **514 script invocations**, including 77/77 typed
  goldens under each compiler; `test_nominal_methods` adds 21 method fixtures.
- Separate corpus-v2 untyped checks **77/77** under each compiler.
- Real discovered-script harness **22/22** with MVP/JIT selected.
- Current private native security **98/98**; selected cache **8/8 normal plus
  8/8 forced GC**. The compiler-isolation test now checks both a plain callable
  and a nominal receiver snapshot across MVP/Full/MVP fresh runtimes, retaining
  the original cache-owner assertions (**D8.5.1**). Its local runtime owner
  guarantees teardown even on a fatal assertion. The final formatted fixture
  snapshot also passes **8/8 normal + 8/8 forced GC**; both manifest identities
  are retained, with `p5_method_update_places-native-tests-methods-final/manifest.json`
  matching the current fixture source. The CLI source inventory differs only
  at this independently rebuilt native fixture; runtime/compiler source drift
  is zero. Combined evidence is indexed by
  `p5-method-update-places-validation-summary.json`.

This does not close all method combinations, the complete language/GTest gate
or performance acceptance. Lexical/live captures, explicit receiver places and
var-argument dispatch still need their remaining acceptance cases. Views,
foreign/native builtins, dependent/variadic definitions, result-domain joins,
linear-science row carriers and the recorded shared ownership/concurrency
faults remain open. No Radiant/C2MIR campaign or current timing qualification
is claimed.


### 10.46 Descriptor-backed native import metadata (2026-11-06)

The older census records 1,098 repeated builtin-admission messages. A current
DOM failure identifies the root: `import_resolver` resolves descriptor-backed
system functions through `find_dynamic_sys_func_import`, while
`jit_import_get_metadata` only consults the static import table. MVP admission
requires that metadata and therefore refuses valid fixed-arity native module
calls such as `dom.next_sibling`.

The shared metadata lookup now recognizes that same descriptor-backed symbol
set. It declares the actual public Item result (**D7.6.1**) and a dynamic scalar
result class, while retaining conservative GC, reentry and exception effects
(**D7.1.3**). Unknown symbols still fail lookup. MVP's existing registry-based
fixed-arity lowering consumes this shared repair; no backend allowlist or
interpreter fallback is introduced. Runtime type/data layouts are unchanged.

`p6_native_module_metadata` builds only `mir.c` relative to the preceding engine.
Its first 11-fixture DOM matrix is **96/99**; only `dom_edit_plan` remains,
exposing an independent scalar-join lowering failure. The dedicated new
`mir_mvp_module_builtins` fixture verifies direct published native signatures
with one through five arguments, including document selection boundaries and
mutation. The initial authoring attempt used first-class builtin references;
all tiers reject those at the existing E229 Core dynamic-ABI boundary. That
unsupported reference route is unchanged; the valid fixture exercises the
published direct-call ABI. Keep that initial diagnostic separate from the
later direct-call result.

### 10.47 Scalar conditional outcomes and wider package controls

MVP already widened pointer `if` joins when an emitted branch had a boxed
outcome, but retained inferred native scalar joins. `dom_insert_html` exposes
the Boolean case: the source infers bool, while the actual C builtin producer
publishes an Item. `_is_clip_def` in the PDF package has the same problem.

Generalized that existing emitted-carrier join decision to all scalar lanes
(**D2.3.4/D2.3.5**). A boxed outcome remains Item until its authorized enclosing
boundary; a join originally allocated as floating-point receives a separate
I64 result register. This preserves Error alternatives instead of decoding
them as native values (**S7.9.3**). Neither source contracts nor builtin return
contracts are strengthened by this change.

`mir_mvp_scalar_joins` pins Boolean, integer, float and Error-valued selection.
After correcting a missing statement separator in the new fixture, the old
metadata-only engine refuses it **0/3**, reproducing the Boolean conversion
failure. The repaired engine and Full/T0 pass both new fixtures **18/18**.
The DOM command registry then passes all three tiers and GC modes. A broader
four-fixture package diagnostic is **33/36**: PDF image/interpreter, citeproc
data and MathML pass, while `test_latex_citeproc_errors` reaches a new MVP
runtime failure, `expected array, got array`, in all three GC modes. Full/T0
retain its unchanged golden. This is open correctness work, not a passing
package aggregate.

Current private release `p6_native_calls_joins-release.exe`, SHA256
`253938ce884899e7b0af13bbbb5607f4d38787508c2ce50d1dd666ee818d4826`, uses the
existing 769-unit release graph and 3,543-file source inventory on HEAD
`b8e29b077` plus recorded uncommitted changes. Runtime/compiler drift is zero;
the latest native test fixture has its own snapshot below. Package inputs
(943 files) have zero observed drift.

- New native-call/conditional family: **117/117** tier/GC checks.
- Wider package diagnostic: **33/36**; combined diagnostic **150/153**.
- Eleven Python methods: **592 script invocations**, including 77/77 typed
  goldens under each MIR compiler. Separate untyped v2 runs: **77/77** each.
- Real harness: 16 selected scripts pass; the separately selected citeproc
  error fixture fails. Retain both JSON files as **16/17**, not an all-green gate.

No aggregate Lambda baseline or current performance qualification is claimed.
A short monitored typed comparison with `p5_method_update_places` completes
with all 77 outputs retained, ten slower flags and twelve faster flags. Of
76 usable ratios, 39 medians are higher and 37 lower; their geometric mean is
0.99763. Sumfp's near-zero samples cannot produce a meaningful ratio. The
`p6-native-calls-joins-typed-regression-screen*.json` artifacts retain the raw
samples and flags. These self-reported workload times use three measured
pairs and one warmup, without Full measurements; they cannot replace the
primary/confirmation campaigns or clear the existing v6 confirmation set.

### 10.48 Effective native VFS audit and corrected cache evidence

The first native metadata unit reports the old `EXPECT_EQ(reentry, YES)` even
though the current source checks that reentry is not `NO`. The private native
builder appended a fixture mapping after the CLI source overlay's mapping of
the same path. Clang selected the first mapping, so copying/hashing the new
fixture did not prove the compiler read it. This also invalidates §10.45's
claim that the extended nominal-method cache assertions had executed; the
base eight tests passed, but the added case was shadowed. Preserve the earlier
reports and their limitation instead of applying later results retroactively.

The private builder now removes the old mapping before adding the native
snapshot, asserts unique virtual paths and retains virtual source names so
relative includes resolve through the intended overlay. The final audit finds
exactly one mapping to the current native snapshot. Both native executables
are rebuilt under `p6_native_calls_joins-native-tests-native-effective/`.
Current effective builds pass **98/98 security**, and **9/9 selected cache and
metadata tests normally plus 9/9 under forced GC**. The metadata unit verifies
boxed results, dynamic scalar classification and conservative effects for
three descriptor entries, plus rejection of an unknown symbol. The expanded
MVP/Full/MVP cache test now genuinely exercises nominal method snapshots across
fresh runtimes. Two view cache cases remain outside this selected lane.

Authoritative native reports use `p6-native-calls-joins-native-effective-*`;
the corresponding `manifest.json` and unique effective `overlay.json` identify
the compiled test sources and runtime objects. Earlier `native-imports`
reports are superseded for newer fixture assertions.

### 10.49 Inferred record fields are not constructor contracts (2026-11-06)

The citeproc failure from §10.47 reproduces during `style` import alone.
Its `ENUMS`/`ATTRIBUTES` initializers fail; standalone arrays pass. The minimal
control `{names: ["a", "b"]}` fails only MVP, while a computed-key map and
Full/T0 pass. The homogeneous inferred array retains its first literal's
element type. `binding_value` supplies the inferred map shape as a destination,
and `map_literal` mistakenly admits every field against that observation.

Only trusted declared destinations now select constructor admission. Inferred
descriptors still select storage; they add no runtime obligation
(**D4.3.1–D4.3.3**). The new `mir_mvp_inferred_record_fields` fixture includes
string, int, bool, float, mixed and nested arrays, inferred function results,
and an explicit array contract that must still reject invalid input. Old p6
fails the fixture in all three GC modes; Full/T0 retain its golden.

Private p7 release SHA256
`5bfa508c0656caa6143bb33e2a63f7263857edcda686c25d9f1b6e1167132ba2`
passes **36/36** focused tier/GC checks, **616** Python script checks and all
77 typed/untyped outputs under each MIR compiler. Citeproc's unchanged error
oracle now passes. Evidence: `p7-inferred-record-fields-*`.

### 10.50 Named and positional decomposition

MVP now emits comma decomposition (**S16.8.7**), using the canonical target
entries from the shared AST. The RHS runs once; each target captures its
projected child and owns scalar bytes across later projections (**S9.1.2,
D6.2.3**). Extracted the projection call into `em_decompose_item`, used by
both MIR emitters. Targets retain Item transport without an inferred source
contract. Globals use the existing module storage path; local targets receive
their declaring activation (**D8.2.4**). Synthetic targets cannot supply an
initializer-based contextual constructor proof.

Initial lowering passes **27/27** tier/GC checks. Expanded coverage adds an
unread target, exposing a shared-index omission: synthetic targets acquired
binding IDs only through identifier uses or assignments. The index now
publishes all decomposition entries at the declaration. Binding publication
can grow storage independently of the structural node count. A native unit
publishes 320 targets across two nodes and checks identity reuse. Its effective
native build passes with the current test snapshot.

Current `p8_decomposition_indexed` release SHA256
`2c3cb863e51397e414361c7a468a1fe51605c6800f4c46594f1c2ad1da61011a`
passes **54/54** focused tier/GC checks and **21/21 real GTests under each MIR
compiler**. Cases cover lexical and global bindings, missing members,
shadowing, COW projections, rebindings, wide scalars and a procedural RHS
evaluated once. The pre-index expanded candidate retains its **51/54** failure
report; it does not qualify the repaired release. Mutable/live capture
decomposition still depends on the unimplemented shared-home path.

Final focused validation retains **77/77 typed and 77/77 untyped** v2 outputs
under each MIR compiler. Thirteen Python methods pass **640 script checks**.
Current effective native index/cache/metadata tests pass **10/10 normally and
10/10 with forced GC/poison**, and the security suite passes **98/98 with MVP
explicitly selected**. A first security run omitted the selector and used
Full; it also finishes **98/98** before a proposed stop, so no process is
terminated. Keep its report as a Full control, not MVP evidence.

The validation summary records binary/source/runner hashes, package inputs,
fixture hashes and unique effective native VFS mappings. Runtime/compiler
drift is zero; the native index unit is newer than the CLI inventory and is
verified through its separate snapshot. No current performance acceptance
or aggregate Lambda baseline is claimed.

### 10.51 Broader MVP fixture census and retained failures

Rebuilt the real discovery harness from p7's frozen current source, using the
unique effective VFS mappings from §10.48. A selection covering all 87 then
present MVP fixtures plus five matching controls runs **92** scripts. MVP
passes **71**, Full **63**; **16** failures are shared, five are MVP-only and
thirteen Full-only. These are diagnostic results, not baseline qualification.
The shared set includes source-contract failures and existing output
differences; no scripts, goldens or failure accounting are changed.

The MVP-only set contains `copy_loops`, `iterations`, `kernel_loops`,
`inferred_record_fields` and `scalar_var_lanes`. Three time out. The other
two finish their script but are charged abnormal process exits by the
harness's existing teardown check. The new record fixture passes a separate
one-case real harness run (**1/1**); this does not clear its batch failure.
Keep the root cause open. The subsequent p8 21-case positive selection passes
both policies, but does not replace this broader failing selection.

Reports: `p7-inferred-record-fields-real-{mvp,full}-gtest.json`, their summary,
and `p7-inferred-record-fields-isolated-gtest.json`. Existing MVP fixture and
recorded package input hashes remain unchanged during the run; the census is
still narrower than the aggregate Lambda gate.

### 10.52 Store containment follows the receiver binding (2026-11-06)

All three MVP-only loop timeouts reproduce with p8 in short, isolated runs.
PTY capture shows the last completed operation: the next call attempts a
rejected array store. MVP uses the current statement scope, abandoning a
loop body's tail and its counter update, then repeats the same iteration.
Full selects the receiver binding's declaring region (**S7.7.6v2**).

Array, member and projected-place stores now select that binding's scope;
parameter/module roots retain callable containment. Inlined calls retain
their own completion boundary. The same error branch emits the existing
retained/partial-state diagnostic with the receiver name and skipped tail.
This changes neither whole-binding continuation (**S7.7.4v2/S7.7.5v3**) nor
the destination's store admission contract.

`mir_mvp_store_containment` covers parameter arrays, outer and inner block
locals, fields, nested paths, partial writes and COW snapshots. Full/T0 pass
its initial control **6/6**. Private p9 SHA256
`961fd0356f6db04b343710da13dd1c2a2c0261bd86fdcf4e057ed54b0a1ace72`
passes the new fixture, all three previously timed-out fixtures and
`ledger_containment`: **45/45** tier/GC checks. Fourteen Python methods pass
**670** script checks; all 77 typed/untyped v2 outputs remain valid for both
MIR compilers. A first private build catches an omitted source argument at
one projected-var publication call; the corrected p9 build is authoritative.

The broader real harness is **80/95 MVP versus 66/95 Full**. All fifteen
remaining MVP failures also fail Full; no MVP-only failure remains in this
selection. The earlier two abnormal batch exits no longer reproduce here;
do not infer a separate lifecycle root cause from that observation. Existing
fixture/package hashes remain unchanged. Reports use `p9-store-boundaries-*`.

### 10.53 Confined nested-procedure capture homes

MVP now consumes the shared confinement facts for parent-fn locals
(**S9.1.4v2, D7.2.3**). Each executing declaration, including decomposition,
creates the runtime's existing owned-slot home. Confined pn environments
capture that home; pure functions and escaping closures copy the current
value. Snapshot capture rules and the pn-from-fn prohibition remain enforced
by the shared builder. Legacy pn-parent mutable snapshot write-back stays
open and is still explicitly refused.

Shared bindings use Item transport while their original annotation continues
to own admission. Shared-home reads/allocations enter the GC effect plan.
Publication updates the home; post-call reloads use raw loads without
overwriting a pending scalar companion. Cached layout/ownership witnesses
are invalidated on reentry. Active homes follow the lexical scope stack,
so callbacks after a branch cannot read an unexecuted or exited local.
Home-owning functions retain an activation boundary for precise ownership;
the existing native-call route handles them. Shared home construction and
noncalling reload emission are extracted for both MIR frontends.

Six existing fixtures fail the old p9 MVP **0/18**. Current p10 passes those
plus `mir_mvp_confined_capture_homes`: **63/63** across three tiers and three
GC modes. This includes scalar tails, COW children, decomposed targets, fresh
loop homes, creation-time snapshots, typed numeric admission and branch exit.
The first version of the new fixture omitted an explicit `any` return on a
snapshot of a deferred float outcome; all tiers correctly reject it with
E208. The source now admits that outcome explicitly; its golden is unchanged.

Private p10 SHA256
`bb80290be0e68a7ed7c1567f30396be1361221c8b33a93e667e2f21be02e8b3b`
passes **712 script checks** in fifteen Python methods. The expanded real
harness is **87/102 MVP versus 73/102 Full**, retaining the same fifteen
shared failures. The native compiler-isolation test adds a shared-home and
pure-snapshot image across MVP/Full/MVP runtimes; final effective native
results are recorded below. No aggregate Lambda or current performance
acceptance is claimed.

### 10.54 Nullable capture outcomes and final checkpoint

Extended the new fixture with retained validation errors and nullable int,
float and bool snapshots. After replacing two reserved type words used as
local names, MVP and T0 agree; Full captures nullable bool null as integer
`2`. The old p9 Full reproduces the same value in all GC modes, proving it
precedes the shared-emission refactor.

Full had already decoded the Boolean initializer into the 0/1/2 lane but
rebuilt its producer descriptor from the original AST expression. Boxing
that value into a shared home loses the nullable bool contract. The producer
now retains the admitted physical lane and its nullable contract
(**D2.3.4–D2.3.5**), preserving null through the existing boxing operation.
No runtime value layout or source contract changes.

Final private release `p10_capture_outcomes`, SHA256
`b6bdd2d85bc018b0bf4e2da2a355e7c8b42e27088f5eec5dc6cbc5a9b7573051`,
passes **108/108** focused tier/GC checks and **712** script checks across
fifteen Python methods. Both MIR policies retain **77/77 typed and 77/77
untyped** benchmark outputs. Effective native index/cache/metadata tests
pass **10/10 normally + 10/10 forced GC/poison**, including the new
MVP/Full/MVP shared-home image case; security passes **98/98 with MVP
explicitly selected** and its interpreter controls retained.

The broader real GTest selection remains **87/102 MVP versus 73/102 Full**.
All fifteen MVP failures also fail Full. Retain that failing selection;
these results do not qualify the aggregate Lambda gate. Current source,
fixture and package drift is zero. The final validation summary records
source/runner hashes and verifies the native overlay's unique effective
test mappings. Current performance qualification and the full goal stay open.

### 10.55 Variadic definitions, positional slots and call completion

MVP now admits variadic functions and procedures through the shared boxed
entry protocol (**D7.2.2, D7.4.1**). Fixed parameters retain their signature
positions and `Function.arity`; a trailing Item carries the rest array.
Direct calls package extras after fixed-argument admission. Dynamic calls
use the existing signature adapter, and imported definitions and bound
methods retain their declaring context. Rest definitions remain outside
MVP's current inline and dense-call expansions, whose state does not model
the receiving rest activation.

`em_argument_array` owns physical argument construction for Full rest calls,
MVP direct rest calls and MVP dynamic calls. The verbatim append preserves
nulls, adjacent text and each list's array image (**D3.3.1, S2.5.6**). Named
resolution now counts supplied extras as well as fixed slots, while formal
proofs and var-place checks stop at the fixed parameter list.

Both MIR policies keep previous/current rest owners in fixed precise frame
slots; ambient `EvalContext.current_vargs` is not a GC root (**D6.1.1,
D6.3.1**). Their common completion restores the previous context, including
parameter rejection and explicit return. Full's wrapper installs the
receiving context before defaults; its existing default-activation scan now
recognizes `varg()`/`varg(n)`. Noncollecting bodies still reserve and initialize
their owner slots. An intermediate register-root implementation failed
`func_param` because the root finalizer removed its root frame; that candidate
is superseded by fixed ownership, rather than an artificial liveness read.

The existing Full/T0 self-tail transform rebound only fixed parameters and
discarded rest operand evaluation. It now builds and publishes the recursive
call's new rest array before rebinding fixed slots (**S16.9.7**). The two
tracked `interp_variadic*.txt` tail results change from `[2, 7]` to `[1, 1]`.
`p11-variadic-oracle-repairs.json` retains old/new text and hashes. These are
core oracle corrections; the frozen v2 benchmark corpus does not change.
The new admission case also exposed Full's unchecked declared tail operand
and T0's premature rest evaluation. Full checks an unproved non-error
declared operand; T0 evaluates extras after fixed admission (**S7.7.3**).

Five new executable fixtures and one companion module cover positional
null/text/list values, wide scalar ownership, defaults, nested restores,
closure values, imports, bound methods, self-recursive effects, rejection
order and var rebinds. Every new script has its paired golden. The native
cache-isolation case adds a variadic/default/tail image across fresh
MVP/Full/MVP runtimes. The new Python methods retain the Full var-rebind
failure as a recorded control: MVP passes `mir_mvp_variadic_exits`, Full
returns the old caller value, and forced T0 excludes that var/rest call.
The oracle and failing controls remain visible.

Private release `p11_variadic_admission` (O3 ThinLTO, no DEBUG), SHA256
`8b0e4ae983c7ee759b8c2a46d9f7d90e205081cb687eaefda0c5baeaf1a828b9`,
passes **78/78 MVP** tier/GC checks across 26 fixtures. Full and T0 each pass
75/78; the six controls above are the only failures in the 234-row matrix.
AUTO handoff passes **9/9**. Seventeen Python methods pass **793** script
checks, including **77/77 typed** goldens under each compiler; canonical
untyped outputs also pass **77/77 each**. Native cache/index/metadata passes
**10/10 normal + 10/10 forced GC/poison**; security passes **98/98** with MVP
selected. The real harness expands from 102 to 117 cases: **102/117 MVP,
87/117 Full**, with the same fifteen shared MVP failures and no newly failing
row on either compiler's original population. Source, recorded-input and
benchmark-corpus drift is zero; native effective overlays are verified.

Evidence: `p11-variadic-final-validation-summary.json`, its tier/GC, AUTO,
Python, native and real-harness reports, and the frozen release manifests.
These are focused correctness results. Language/GTest parity, dependent
definitions and current performance gates remain open.


### 10.56 Boxed dependent definitions and first-class binder values (2026-11-06)

MVP now emits dependent definitions through a boxed entry rather than rejecting
all signatures containing binders. The activation allocates the selection
slots in the existing fixed precise-root suffix. Every site admits its value
before publishing a selection, and dependent reference parameters are checked
after the sites have joined. Defaults, body boundaries and declared dependent
returns use that activation's environment (**S11.4.8v2, D4.1.4**). Dependent
annotations remain receiving contracts; value observers retain an open Item
representation until an independent proof permits narrowing (**D2.3.1,
D4.3.3**). No additional interpreter delegation was introduced.

The written dependent return contract must survive success-domain planning.
Removing its error alternative had also replaced a `TypeBoundRef` with its
copied `type` bound, causing a returned number to be checked as a type value.
MVP now checks the written contract in the callee and publishes an ordinary
boxed result to the caller. Binder names in closure environments retain the
selected first-class type value, rather than reading an unrelated child's
slot. Empty nested sites read their declared bound, as required by
**S11.4.8v2**; Full and T0 share that fallback.

Type-expression classification is shared with the AST builder. A type-valued
expression such as `int` cannot take its represented type's integer carrier:
that had incorrectly packed `[int, int, 40]` as a numeric array. Forwarding a
runtime type value now retains deferred admission instead of requiring literal
syntax. Static binder collection uses a conservative approximation for a
runtime type value or nested site and refuses to use that approximation as an
exact specialization key. Nested array arguments retain their whole written
container contract; the site's scalar bound is not the argument's contract
(**S11.4.8v2, D2.3.1, D4.1.4**). A negative fixture still rejects an integer
passed to `T: type` with E207.

An admission result needs more information than the Item's error tag.
`any` and `error` may admit Error as data (**S11.4.3**), while an incompatible
repeated binder selection must still fail. The shared admission kernel now
optionally reports a separate Boolean status through
`lambda_type_check_env_status`, without repeating the check or changing Item
layout. An unchanged Error fast return reports whether it was admitted; a
binder site still performs the walk that publishes its selection. MVP reuses
one native status cell per activation, allocated at entry so loop checks do
not grow the native stack. Existing boundary APIs retain their signatures
and rich failure diagnostics. The native regression uses the canonical
`alloc_type_kind` creation path, which publishes the process's binder-presence
flag before a binder-bearing contract can be observed.

New paired fixtures cover dependent defaults and imported/dynamic calls,
forwarded type values, wrong dependent returns, deferred local reassignment,
empty nested sites, repeated-site joins, captured type names, escaping
first-class type snapshots, wide scalar lifetime and Error-as-data admission.
The helper module and E207 negative fixture also have paired goldens.
`NegativeScriptTest.TypeBinderDiagnosticsRemainSpecific` includes the new
rejection on interp/JIT/AUTO; a native admission test distinguishes accepted
Error data from failure and checks that a rejected join preserves its prior
selection. The cache isolation test alternates MVP/Full/MVP across fresh
runtimes and verifies independent invocation selections. Its possible error
outcomes are explicitly propagated under **S7.5–S7.6**.

Qualification uses `p12_binders_qualified-release.exe` (release O3 ThinLTO,
769 units), SHA256
`47e6865187f0d4f43674ec5a4d7b26dd3da0761881cf39084e1cafede21deade`:

- Tier/GC matrix: **111/111 MVP**, **102/111 Full**, **102/111 T0** across
  37 fixtures. AUTO checks pass **12/12**. A later captured metadata import
  is separately rechecked **9/9** with source/golden dependency hashes.
- Twenty Python methods pass **853 script invocations**, including all
  77 typed goldens under both compilers. All 77 untyped v2 goldens also pass
  each compiler. These are output checks, not speed measurements.
- Native cache/index/import metadata pass **10/10 normal and 10/10 forced
  GC**. Admission/diagnostic checks pass **4/4 normal and 4/4 forced GC**.
  Security passes **98/98 with MVP selected** and interpreter controls retained.
- The real harness passes **114/129 MVP and 97/129 Full**. All twelve new
  rows pass MVP; no prior passing row regresses on the common 117-case
  population under either policy. The same fifteen MVP failures also fail
  Full; none is dropped or relabeled as supported.

The non-MVP matrix failures remain visible: `mir_mvp_binder_values` exposes
Full's dependent numeric conversion/type captures and T0's captured binder
environment, and `mir_mvp_binder_outcomes` exposes admitted Error completion.
The values fixture has matching failures under the earlier p11 Full/T0
controls; the outcomes fixture has recorded before/after controls. Full/T0
also retain the variadic var/rest failures from §10.55. No shared failure is
used as a reason to weaken a golden.

`p12-binder-qualified-validation-summary.json` records binary, runner, native
object/overlay and result hashes. The 3,543-file CLI source inventory has no
engine drift. Two native test units were corrected after that inventory and
are qualified through separate current VFS snapshots and the same frozen
engine objects. Their old failed runs remain as diagnostics; only the corrected
native runs qualify those fixtures. All 1,208 recorded inputs and 483 frozen
corpus files are unchanged at final audit. No existing oracle or benchmark
source changed in p12. Full language closure, aggregate Lambda GTests and
current typed/untyped performance targets remain open.


A follow-up diagnostic retains an additional boundary-ordering gap:
`observed(T: type, value: T, witness = type(value))` returns an integer type
witness for `observed(float, 1)` under all three tiers, while admission of
`value` converts it to float. The temporary expected-value probe records
**0/9** tier/GC matches in `p13-binder-default-observer-before.json`; it is not
part of the passing p12 population. The MVP parameter pass defers dependent
reference admission until after later defaults have evaluated. Resolve the
interaction between repeated-site joins, default dependencies and converted
parameter reads before claiming full dependent-call coverage
(**S11.4.8v2, S11.4.6v4**). No shared failure or oracle was changed.


### 10.57 Named system operands, numeric type calls and empty normal domains

**2026-11-06.** A fresh paired census identified three separate call-boundary
families. MVP rejected named system operands, treated explicit sized/u64
conversion types as Function values, and crashed during closed-domain planning
when a call argument contained only Error. These are implemented without new
interpreter delegation.

Named system operands now use the existing shared interpreter planner's
system-call admission predicate. Their AST wrappers forward the child value
and descriptor; system operands keep their source order and registry ABI.
This covers the existing `skip_null` aggregate forms without fabricating a
second option resolver. Dynamic Lambda named calls retain the declaration
requirement (**S12.3.2**).

One shared AST classifier identifies the numeric Type producers that Full
already intercepts for explicit conversion. MVP, Full and T0 use that
classifier; both MIR compilers use the same physical conversion emitter.
The returned Item retains conversion errors before any caller propagation,
without conflating explicit conversion with declared-contract admission
(**D4.2.1, D2.3.1**). The new `mir_mvp_numeric_type_calls` fixture checks
signed/unsigned wrapping, f32, a wide u64 result, Error transport and retained
array values under GC.

The admission crash was localized with small source reductions and an O3
release image with local symbols retained. Its backtrace ends in
`update_closed_values`; this was a compiler-planning crash, before a MIR dump
could be produced. `lambda_type_remove_error` returns NULL for an error-only
contract. The planner dereferenced that result while choosing a private
parameter lane, even though the implicit parameter rejects that argument
before the function can be entered (**S7.7.3**). The same root cause was
reproduced in an Error-bound bitwise argument, a successful-return domain scan
and an inferred mutable Error binding.

The shared private planner helper now normalizes that empty success domain to
`none`. Error-only call edges and exits provide no successful carrier evidence;
other observers can safely inspect the bottom type. The receiving contracts,
Error values, call effects and failure transport stay intact (**D4.3.1,
S11.1.7**). `mir_mvp_empty_normal_domains` combines all these cases with normal
calls, so the regression covers both the rejected edge and the surviving
successful lane. The original `admission_call_outcomes` now matches its existing
golden across all three tiers and GC modes; no oracle was changed.

Qualification uses `p13_empty_normal_domain-release.exe` (O3 ThinLTO, 769-unit
release graph), SHA256
`367b7a65f45f9cdaf6780c1ae506f89ef2d390be3ea40825c6bda43f3d47c3cd`:

- **129/129 MVP** tier/GC checks across 43 fixtures; Full and T0 each pass
  **120/129**, retaining the nine controls from §10.55–§10.56. AUTO passes
  **30/30**. The original fixtures remain in the population.
- Twenty-one Python methods pass **877 script invocations**. All 77 typed
  goldens and all 77 untyped v2 goldens pass under each compiler.
- Native cache/index/import metadata pass **10/10 normal + 10/10 forced GC**;
  admission/diagnostic checks pass **4/4 + 4/4**; security passes **98/98**
  with MVP selected and explicit interpreter controls retained.
- The real harness passes **120/135 MVP and 103/135 Full**. Six new rows
  include the two new fixtures plus `agg_null_absence`,
  `admission_call_outcomes`, `error_conversion_outcomes` and
  `fallible_scalar_member_call`. All six pass both policies. No prior passing
  row regresses in the common 129-case population; the same fifteen MVP
  failures also fail Full.

`p13-call-forms-validation-summary.json` records the executable, runner,
fixture, native object/overlay and report identities. The 3,543 source files,
1,220 recorded inputs and 483 frozen corpus files have zero drift at final
audit. Separate conversion-family screens retain the still-failing image and
imported-type cases; they are not silently included in passing counts. This
qualifies focused correctness, not the aggregate Lambda baseline or current
speed targets.

The frozen p12 discovery census is independent of this p13 qualification.
`run_discovery_census.py` records 2,123 discovered tests, copies the Lambda/lmd
inputs, uses corpus v2 benchmark sources and hashes 9,044 inputs in total.
It compares 50-case selections under both compiler policies through the real
harness, with separate reports and an explicit supervisor deadline. A stalled
or unreported compiler chunk remains unresolved; its cases are not counted as
passing. The first audit has 700 paired reported cases (**515 MVP, 603 Full**),
no input drift, and one unresolved MVP chunk. The live runner continues beyond
that audit. `p12-current-paired-census-summary.json` records the exact compared
population and partial status; do not combine it with p13 as one experiment.
Shared chart-package failures include frontend E312 on a range-index/len
comparison before either compiler runs. They remain visible shared failures,
not evidence that an MVP lowering route has been exercised.

An authority audit also distinguishes the old capture-write compatibility
code from valid new coverage: **S9.1.4v2** permits writes only in the confined
parent-fn case and otherwise retains immutable snapshots. Full's legacy
pn-parent write-back code grants no additional language permission. Keep the
capture-admission audit visible; do not remove MVP's rejection merely to
reproduce a nonconforming write-back path.


### 10.58 Imported type values and JavaScript export adapters

**D6.3.2** keeps the emitted-helper effect check authoritative. Imported type
values exposed an incorrect default effect for `base_type`: this helper reads
only the execution context's static literal-type table and does not allocate
or reenter. The registry now records that bounded read as noncollecting. MVP
can compile `import_type_values.ls` without weakening the ratchet.

**D7.4.1–D7.4.3, D6.2.3** require the foreign callable and its result owner to
cross their actual ABI. A JS import symbol denotes a hosted function object,
not a Core code address. The synthetic declaration publishes an arity but has
no Core AST parameter chain. MVP now reads its declared arity, imports the
hosted object under its actual symbol, and emits a checked Core boxed adapter
for direct calls and function values. It uses Full's existing bridge range
(0–8 arguments); larger foreign signatures remain rejected. Both MIR compilers
share `em_js_export_call`, which precisely roots the hosted callable and owns
the result's scalar home. This adds no Lambda interpreter delegation.

**S5.5.1v2, D7.2.1** govern adapter identity. A cross-import probe showed that
each importer previously created a new synthetic AST for the same guest
namespace. Per-importer native adapters also have different code addresses.
MVP stamps the existing `Function::def` / `def_module` fields with the export
site; equality uses the site when both operands publish one and otherwise
retains the existing native-pointer path. The canonical `ModuleDescriptor`
retains its synthetic import Script, distinct from the guest's parsed Script
at the same file path. The runtime owns that AST and clears the descriptor's
borrowed pointer before retiring it. It is never a process-wide AST cache or
a new GC root. JS-tainted import cones already exclude sealed MIR images.

The new paired fixtures `mir_mvp_js_exports` and `mir_mvp_js_export_values`
exercise direct bridge arities 0–8, cold floating results retained across
allocation, text, guest module state, errors, saved/array-held functions,
higher-order calls, closure captures and cross-import function identity.
`_mir_mvp_js_export_module.ls` has its own empty golden because it has no
entry expression. Its initial newly authored null oracle was corrected after
both compilers emitted no output; no existing oracle changed. The initial
and repeated real-harness reports remain separate. The native fresh-runtime
case covers state isolation and cross-import equality in three executions.
Full/T0 still reject ordinary calls through imported JS function values with
E229; those controls keep their semantic oracle and remain visible failures.

Qualification artifacts use prefix `p14-foreign-descriptor` under the task's
temporary directory. The current release is frozen before broad validation;
initial identity probes pass the three GC policies. The final 49-fixture matrix passes **147/147 MVP**, with **135/147**
under each control. AUTO/production passes **45/45**. Twenty-four Python
methods pass **910 script invocations** (including 77 typed goldens per
compiler), and untyped goldens pass **77/77 each**. Native checks pass
**11 + 11 forced GC** cache/index/import cases, **4 + 4** admission cases,
and **98** security cases with MVP selected. The real harness passes
**127/142 MVP, 109/142 Full**, with no prior passing row regressing. The
same fifteen shared failures remain. All 1,239 recorded inputs and 483
frozen corpus files are unchanged after the final checks; engine/native
source provenance is verified. Final AUTO controls use production: the
explicit MVP policy continues to require JIT, and the deliberately rejected
AUTO/MVP attempt remains a separate diagnostic artifact. Aggregate Lambda baseline and current performance remain
unqualified. This milestone does not establish all-language parity.


### 10.59 Complete coordinate reads and temporary storage ownership

**S8.2.4v3, S7.1.1v3, D2.3.4** require an N-D read to consume its entire key
list and keep an invalid receiver, wrong rank or invalid coordinate as null.
MVP previously emitted its single-index reader for a multi-key AST node,
returning a leading-axis row and skipping later key effects. The shared
`em_index_nd` stages every native coordinate and calls the existing runtime
reader. Full preserves its existing per-coordinate native conversions; MVP
uses the existing checked `fn_int64_index`. Both evaluate the receiver before
the keys and preserve its `last` binding. Multi-key reads no longer acquire
a first-row value/range proof or an ASCII-character specialization.

**D6.3.1, D6.3.4** distinguish the rooted receiver from raw temporary coordinates. A
collecting-key probe failed under both frozen p14 Full and the initial p15
candidate: `heap_data_calloc` produces data-zone bytes without an independent
object owner, so rooting their raw address cannot keep them alive. Coordinates
now occupy fixed native-frame storage allocated once per lexical read, outside
precise GC roots. The receiver remains precisely rooted across every key.
`em_fixed_stack_storage` also replaces the existing fixed bitcast fallback and
MVP admission-status allocation shape. This introduces no stack scanning.

Nested row storage is an MVP representation optimization. Its existing tensor
requirement now includes multi-key observations, immutable aliases and direct
call arguments, rather than coordinate writes alone. This makes the existing
`ndim_any_receiver` case work while retaining the prior row representation for
other consumers. It is not complete N-D construction support: the separate
`p15-ndim-literal-screen.json` retains three MVP failures (`typed_array_ndim_index`,
`typed_array_ndim_literal`, `typed_array_ndim_dynamic`). The first still exposes
unnormalized inner rows of 3-D literals; shape observers and opaque row producers
need a broader canonicalization proof. `typed_array_ndim_iter` passes that screen.

New paired fixtures `mir_mvp_ndim_reads` and `mir_mvp_ndim_effects` cover scalar
and cold-float results, all coordinates, misses, `last`, allocating keys and
observable receiver/key evaluation order. Together with the existing dynamic
receiver and seven image fixtures, **30/30 MVP and 30/30 Full** checks pass over
three GC policies. T0 retains its explicit index-expression exclusions in the
control report. `p15-ndim-owners-family.json` records that focused result.
The frozen candidate's final 59-fixture matrix passes **177/177 MVP**;
Full passes **165/177**, T0 **156/177**, retaining their existing controls and
index exclusions. AUTO/production passes **75/75**. Twenty-five Python methods
pass **970 script invocations**, including all 77 typed goldens under each
compiler; all 77 canonical untyped goldens pass under each compiler too.
Native cache/index/import checks pass **11 + 11 forced GC**, admission checks
**4 + 4**, and security **98/98 with MVP selected**. The real harness passes
**137/152 MVP, 119/152 Full**, adding ten passing MVP rows with no prior passing
row regressing; the same fifteen shared failures remain. All 1,262 recorded
inputs and 483 corpus files have zero drift. Engine/native effective-source
provenance is verified in `p15-ndim-owners-validation-summary.json`. Existing
semantic oracles remain unchanged. Aggregate Lambda baseline and performance
qualification remain open.

The independent frozen p12 census also finished its 86 compiler chunks. Five
chunks retain six missing/timed-out reports (250 unpaired cases); they do not
become passes. Its 1,873 paired reports have 1,334 MVP and 1,637 Full passes,
with zero input drift. The disk-interrupted unsaved attempt was preserved and
repeated after reclaiming superseded task link archives; source objects,
executables and evidence remain. No census process is still running. This
older diagnostic is not combined with p15 as a current whole-corpus gate.


### 10.60 P16: canonical N-D values, declaration snapshots and file outputs

Canonical array construction now calls `array_end` unless the shared AST index
proves every use consumes scalar coordinates or length, including closed
forwarding. Shape observers, escaping/opaque producers and nested children use
the canonical N-D representation (D3.2.2, D4.3.1). Public `len` on a tensor uses
`fn_len_l` rather than the total payload count. Named lexical declarations own a
rooted Function snapshot; direct recursion retains its environment, sibling
captures retain the Function, and later aliases reuse it (S9.1.4v2).

File elements initialize their evaluated destination and retain append/finish
errors as the constructor's outcome (S2.6.6v3). Normal elements keep their content
normalization. Focused evidence: `p16-tensor-unit.json` (12/15 before the length
fix), `p16-snapshot-unit.json` (15/15), `p16-file-unit.json` (15/15 after the fix).
These are unit checks, not benchmark or whole-language qualification. No
existing golden changed.

### 10.61 P17: native views, event entries and edit model places

View/handler entries are private physical MVP functions with the existing
Context/model/event ABI. The shared AST index supplies their structural
ownership; no alternate semantic type table is introduced (D8.2.4–D8.2.6).
State defaults/read access share `em_template_state_read` with Full. Handlers
reload view state and lexical body declarations in their own activation and
publish state through `tmpl_state_set` (S12.1.3v2). The existing registration
pass now runs for MVP as well as Full, including cached execution contexts.
Model writes in edit handlers call the existing MarkEditor bridges.

`p17-view-unit.json` passes 18/18 checks; `p17-edit-unit.json` passes 12/12.
The native GTest `MvpNativeViewsRetainStateAndHandlerHomes` exercises actual
callbacks, retained state, cold values, errors and repeated fresh mounts.
Its first authored source needed an explicit statement separator. Its next
failure was a test-only Error accessor; after correcting that, the added cold
view-result assertion reproduced a pending Item escaping the native template
boundary. The runtime fix resolves the existing companion-slot transport before
returning to the C caller (D6.2.3, D7.4.2). The subsequent edit unit caught a raw-C ABI error in the new auto-commit call:
its optional description takes a null C pointer, not a tagged null Item. The
fixed call uses `MIR_T_P` and zero. Both native units now pass under normal and
forced GC in `p17-edit-callback-unit-{normal,gc}.json`, covering actual model
mutation as well as callback returns. Four new permanent Python unit methods
pass 120 script checks (`p17-focused-python-unit.log`). Two selected Full state
controls also pass six checks. These are focused checks only. The user's
instruction defers all full sweeps and baseline/performance gates.


### 10.62 P18: scalar var and wide map lanes

The old parameter gate rejected float/pointer `var` locals although transport
already owns a boxed Item home independently of the callee's lane (D5.4.6,
D7.3.2). That gate is removed. Focused place-borrow units then exposed a separate
invariance test rejecting a typed leaf passed to an open untyped `var`. The
callee still admits its incoming value; write-back enforces the leaf's declared
contract. Open value contracts now share one classifier across those proofs.

The nullable-wide map fixture also exposed native-field lowering reading raw
machine integers as boxed Items. A shared direct-lane predicate now restricts
native reads/stores to the representations the physical emitter implements;
other field descriptors retain the runtime decoder (D2.4.2). Initial evidence
`p18-var-unit.json` is 3/12, with all failures retained. `p18-var-lane-unit.json` improved to 6/12: scalar place homes pass, but the
place-borrow fixture showed a key call executed twice and literal wide fields
still used raw construction. `read_place` now consumes the keys already staged
by `place_path`, including subscript `last`; native literal construction shares
the read/write lane gate. The frozen `p18_var_place_values` release passes all four fixtures in all
three GC modes (12/12) in `p18-var-place-unit.json`. The permanent Python method
also passes the same four fixtures under both compilers and all three GC modes
(24 checks). The frozen 3,543 engine source files match the working tree. Full
verification remains deferred until the outstanding language work is complete.

### 10.63 P19: dependent parameter/default ordering

Supplied binder sites are admitted before reference parameters, so repeated
sites have finished selecting their join. The remaining parameter/default
pass then runs in source order; a later default observes an earlier parameter
in its admitted type (S11.4.8v2). Fired defaults use an exact rooted scratch
selection environment, preserving TG18's rule that a default does not bind T.
Optional normalization can copy a binder descriptor, so activation ownership
replaces the old pointer-identity test for body reads. The physical binding's
open observation never replaces its dependent admission contract (D4.1.4).

`p19-binder-default-unit.json` passes 15/15 MVP checks (five fixtures, three GC
modes). `p19-binder-python-unit.log` passes the two selected permanent methods,
57 script checks. The before report retains Full's six false assertions in the
new ten-assertion default-order fixture and MVP's optional descriptor failure;
no existing golden changed. Captured annotation environments remain open.

### 10.64 P20: native handler scoped tasks

Physical native handlers do not have a source-function analysis record. The
shared body scanner is now available through `lambda_ast_plan_body_concurrency`;
private handler entries use it instead of silently omitting task-scope joins
(S13.3.1v2). This reuses the existing syntax and direct-call facts, without a
second semantic walk implementation or interpreter delegation.

`LambdaScriptCache.MvpNativeHandlersJoinScopedChildren` dispatches a native
handler whose nested block starts a sleeping child. The child commits to a
temporary document; the handler's following return must read the committed
value. It passes normal and forced/poison GC in
`p20-handler-native-unit-{normal,gc}.json`, using the frozen P20 engine.

### 10.65 P21: query/pipe resource exits and range-proof crash

Key lists own precise root frames. They now join the existing cleanup chain,
so propagation/admission/return exits and loop transfers retire abandoned
query/pipe resources (D6.1.1). A multi-source query has one break destination,
while continue retains the active source frames and advances its current row.
The shared pipe body callback exposes its key-list resource to MVP; its normal
completion still frees the resource in the shared walker.

An initially authored pipe test used statement blocks as operands, rejected
by both compilers under S16.6.6/S16.6.8v3. The corrected fixture uses admitted
propagation and projected var calls. Its stopped-query case then exposed a
compiler crash: dense-region analysis cast a general source to a binary range.
The proof now first verifies the native range shape and excludes query clauses.
The symbolicated private release link and failing probes remain recorded.

`p21-checked-traversal-unit.json` passes 9/9 MVP checks over traversal exits,
existing query sources and implicit key subjects in three GC modes. Full's
separate control returns zero for the multi-source continue accumulator, where
MVP returns the required 43; that Full control defect remains visible in
`p21-isolated-probes.json`. Existing oracles were not changed.

### 10.66 P22: shared native join streams and grouped tuple rows

MVP now materializes join sources through its retained keyed-source walker,
then invokes the same stable hash/cross-product stages as Full MIR. The physical
join-stream builder and single/multi-key construction are shared in
`mir_path.hpp`; lexical binding remains backend-specific. Optional rows retain
null padding, key/index bindings travel in tuples, and grouping retains the
complete tuple row verbatim (S14.1.1v2/S14.1.3). Grouped cross-products also
select that tuple path in Full, fixing its previously missing group stage.

The initial 15/30 report records the missing structural JOIN_KEY admission and
Full's pre-fix grouped cross-product result. `p22-join-keys-unit.json` then passes
30/30 across five fixtures, two compilers and three GC modes. The new grouped
row fixture was extended with retained cold float/wide integer owners;
`p22-query-python-unit.log` passes 33 checks through the permanent query and
traversal methods. The current frozen release/source identity is recorded in
the checkpoint. No full verification, baseline or performance run was performed.

### 10.67 P23 investigation: lexical owner of dependent annotations

The focused `p23-captured-annotation-before.json` reproduces an outer `T: type`
used in a returned named function's parameter annotation, including an inner
`number as U` that occupies its own slot zero. MVP reports the missing lexical
environment; Full returns `[false, true, false]`. The temporary paired fixture
is an investigation artifact, not a new failing permanent test.

`resolve_type_name` morphs the annotation identifier into a plain type node and
erases its `NameEntry`. Parameter annotation syntax remains on
`TypeParam::type_expr`, but the shared core child visitor does not index that
edge; return annotation syntax is discarded after publishing its contract.
Body expression captures therefore work while annotation captures lack their
owner. Slot and copied bound alone cannot distinguish the outer and inner slot
zero. The next implementation must retain lexical type-use facts in shared
analysis (D8.2.4), then build exact rooted physical environments; it must not
add a forbidden binder back pointer to `TypeBoundRef` (D4.1.4). No frontend/type
model change has yet been made for this investigation.

### 10.68 P23: lexical dependent annotations and aggregate admission

An annotation's reference retains only its stable slot and written bound, as
D4.1.4 requires. `AstBoundTypeUse` now records the resolved declaration in the
function's lexical scope metadata. Alias references inherit those dependencies;
the direct scope publication pass rebinds their entries through its ordinary
identity map. The capture pass consumes those facts, and same-spelling captures
compare declaration identity (S9.1.4v2/D8.2.4).

MVP passes the captured reference identities and exact rooted type-value slots
beside local binder slots. The runtime recursive admission walk carries this
explicit view, so nested checks and callbacks need no TLS or global temporary
state. The TypeBoundRef/Item/container layouts remain unchanged. A shared type
dependency visitor replaces duplicate discovery walks; its function-parameter
case explicitly follows the parameter's contract, since a descriptor can carry
a map TypeId without having a TypeMap payload.

Focused tests exposed two aggregate root causes. The array element accessor
peeled a binder/reference to its bound before element admission, losing both
selection and lexical lookup. It now preserves the semantic element descriptor.
Structural map fields already passed their env-aware checks, so rechecking the
same unbound descriptors outside that environment was both redundant and
incorrect; nominal identity/constraints and binary-schema checks remain intact.
A `T:type` reference denotes the unknown value domain of the supplied type's
payload, so static return checking now defers it rather than requiring a type
Item (S11.4.8v2). Captured defaults, return-only annotations and retained Error
payloads then pass without weakening their expected results.

`p23-captured-final-unit.json` passes the two current permanent fixtures in all
three GC modes (6/6), including same-spelling aliases, nested site publication,
structured conversions and cold numeric owners. The selected Python methods
pass 60 checks, with 54 paired existing binder calls. The six captured checks
also pass through the permanent method; its earlier log predates the final
nested-site assertions, whose current hashes are in the final matrix. Full's
normal controls remain visible in `p23-captured-full-controls.json`. The initial
missing scope-rebinding identity, authored syntax/reserved-name mistakes,
parameter-descriptor crash and aggregate failures remain in their before logs;
no existing oracle was edited. No full gate or performance run was performed.

### 10.69 P24: retain methods in local type declarations

The syntax sink used an absolute function depth of one to recognize a type's
methods. Inside a function that condition was false, and the method never
entered the retained AST or TypeMethod table; all three tiers then tried to
call null. The sink now records the declaration's surrounding callable depth
and recognizes its immediate methods relative to that depth (S12.3.3v2).

`p24-local-method-unit.json` passes the new local/saved receiver fixture and two
existing receiver/method-value fixtures under both compilers and all GC modes
(18/18). The broader lexical-capture probe now reaches MVP's explicit method
environment gate, recorded in `p24-method-capture-after-parser.json`; receiver
plus lexical environments remain the next implementation task, not a completed
feature. The nominal method table must retain only immutable code metadata;
per-activation captures require exact GC ownership and COW preservation.

### 10.70 P25: method lexical environments and writable receiver places

Each dynamic nominal declaration now has a fresh runtime type value, while its
nominal record keeps definition-site identity (S11.3.1v2). A native weak table
attaches a private method-name/Function map to that value and to constructed
instances. The table is an existing collection ephemeron carrier; it is rooted
precisely by the Heap, and weak owner keys do not retain dead instances through
their environments. Bound lexical methods use two owned slots: receiver and
source Function. The source Function already owns the ordinary capture slots,
so capture indices and the shared closure count limit remain unchanged. Saved
fn methods snapshot their receiver; bound equality compares definition and
receiver under S12.3.3v2, excluding private ownership metadata.

The first focused attempts exposed two shared frontend edges: nominal
back-pointers were never assigned, and extension entry edges were rewritten
after index publication. Declaration/literal entries now rebind before their
shared index facts are published (D8.2.4). Method escape flow also follows a
type binding into literals and retained constructors; returning an instance
cannot incorrectly prove its pn methods confined to the factory (S9.1.4v2).
A nominal alias keeps the complete declared descriptor; copying just its
TypeMap prefix into a structural placeholder truncated the method tail.

Untyped dynamic pn member calls previously skipped receiver detachment when
static analysis did not set `is_proc_method`. MVP now resolves the callee once,
then a shared runtime boundary classifies its method metadata and detaches the
root/spine using `cow_path_borrow_keys`. It updates the bound receiver and
publishes the prepared root before arguments execute. The same path handles
projected and computed receivers, immutable-place refusal, and existing fn or
ordinary field callables (S9.1.3v3/S9.3.1). COW map/element clones preserve the
instance attachment without publishing Functions on static TypeMethods.

Native collection storage, stable order nodes, hashing, writes and ephemeron
tracing moved intact from JS into a shared runtime module. JS retains key
canonicalization, dispatch, iterators and observable error policy. Lambda does
not instantiate JS state, and no-JS stubs no longer claim these shared storage
lifecycle hooks. The private command/source inventories explicitly add this
translation unit; ordinary builds discover it under the runtime source dir.

Focused release evidence:

- `p25-native-method-unit.json`: 9/9, three permanent capture fixtures across
  normal, forced-every/poison and seeded/poison modes.
- `p25-existing-method-controls.json`: 30/30 existing paired method checks.
  Its six extra alias attempts used a reserved binding name and are retained;
  after correcting that authored test, `p25-alias-method-unit.json` passes 9/9
  across MVP/Full/T0 without changing expected behavior.
- `p25-negative-method-unit.json`: 3/3 escaped capture-writing method refusals.
  `p25-method-python-unit.log` runs the permanent focused method (12 CLI checks).
- `p25-js-storage-units.json`: 9/9 existing collection/order/advanced scripts
  across GC modes after storage promotion.
- `p25-native-storage-liveness-unit.log` and its GC counterpart: the native
  no-JS-realm ephemeron unit passes, retaining a value while its key is rooted
  and reclaiming a value-to-key cycle afterward. The first test used an
  ownership query as a liveness oracle; the correction checks GC_FLAG_FREED,
  since object-zone ownership includes free slots.

All earlier compiler crashes, storage promotion compile failures and authored
probe mistakes remain in their logs. `p25-native-binary-cleanup.json` records 43
superseded private executables removed, with their hashes retained. No full
Lambda baseline, aggregate language gate or performance gate ran for P25.

### 10.71 P26: scoped nested nominal assembly

The resolver kept one object assembly cursor. A type declared in a method
replaced it, and the inner close then cleared the outer declaration, discarding
its later fields and methods. MVP could crash while lowering the partial AST;
Full/T0 called missing methods. A private `LambdaObjectResolutionState` now
travels with the recursive declaration walk. The inner type gets a fresh
cursor, then restores the enclosing state after its ordinary scope exit
(D8.2.4/S12.3.3v2). Runtime value/type layouts remain unchanged.

`p26-nested-method-final-unit.json` passes 9/9 on all three tiers and GC modes,
including three nesting levels and a wide field after a nested method.
`p26-captured-method-unit.json` passes 12/12, including the original captured
outer-method reproducer. The initial new fixture missed an attribute comma;
that syntax error is retained in `p26-nested-method-unit.json`, and only the
source syntax was corrected. No full or performance gate ran.

### 10.72 P27 investigation checkpoint: admission and result joins

A selected batch of 16 prior failures passes 26/32 paired normal-mode checks
on P26. Full passes every row. The six remaining MVP fixtures are
`admission_runtime_type`, `error_members`, `type_none`,
`proc/admission_borrowed_outcomes`, `proc/admission_return_outcomes` and
`proc/error_union_normal_admission`. Their original sources/goldens remain
unchanged. This batch is diagnostic unit work, not an aggregate gate.

Concrete next investigations (no P27 code has changed yet):

- Dependent admission reports `at value` instead of `at declaration 'value'`.
  Pass the ordinary declaration boundary label into the existing env-aware
  checker; use a shared label formatter if extracting Full's spelling.
- MVP builtin error() lowering lacks Full's `lambda_error_stamp_site` call.
  Promote that emission shape into a shared helper rather than copy it
  (S7.4.4). Constructor `AstSysFuncNode` already carries site_line/site_column.
- The effect fixed point checks explicit RETURN_STAM but misses an implicit
  body return's admission. `passes_through(x) none` and `narrowed_literal()
  int^` emit checked helpers in a planned noncollecting function. Model the
  actual implicit result boundary, preserving proven clean typed paths.
- `proc/admission_borrowed_outcomes` preserves arrays but loses a scalar
  Error when a var call writes a deferred failed admission. The caller's
  register/domain must include the formal's effective outcome; Item borrow
  homes alone do not preserve it when reloaded into an int lane (S7.7.4v2).
- Error-union admission skips normal conversion when membership/subtyping is
  proven: `receive(7)` returns int instead of float, and `flow_number(7)`
  similarly fails. Preserve scalar/record conversion even with an admitted
  Error arm (S11.4.6v4/D4.2.5); inspect `admit()` proven/success_proven paths.

Use `p26_nested_type_state` and the P25 native command/input inventories for
next frozen builds. Engine sources match all 3,545 frozen hashes. Do not run
full verification or Lambda baseline until these and the other known gaps are
implemented; continue focused feature batches only.

### 10.73 P27: complete admission outcomes and implicit return effects

The six focused failures now pass their unchanged goldens. Error unions lift
normal admission before boxing, preserving numeric conversion and record
reification while Error remains data (S11.4.6v4). Inlined parameter reception
reuses the ordinary conversion path. Its var publication now carries an Item,
rather than labeling a post-store Error with the incoming int ABI lane
(S7.7.4v2). Native entry values exclude failed-store Error from their input
carrier, independently of the binding's broader body domain.

Shared block-result classification now supplies the implicit-result effect
boundary and collection allocation fact, alongside explicit returns. Pure
proven bodies keep their existing effect proof; deferred `none`/narrowing
returns cannot emit unplanned checked helpers. Full and MVP share the error()
source-stamp emitter (S7.4.4) and pooled declaration/assignment labels, removing
eight repeated Full label formatters.

`p27-admission-first-unit.json` passes the six original normal cases.
`p27-admission-gc-unit.log` passes the selected permanent admission, scalar-var
place and captured-annotation methods: 66 CLI checks across normal, forced and
seeded GC/poison, including paired Full controls. Current focused release
`p27_admission_var_lanes` has SHA256
`0d1213add437fb58d256d43a9679a131da29fc4bc7f7bdc6749f0325e7603631`.
No aggregate/full or performance gate ran.

### 10.74 P28: member contracts, layouts and functional result carriers

Pattern/range contracts denote member values, not Type pointers (S11.1.2v4,
S11.1.3). Their canonical representation now stays boxed. The shared explicit
Type-value predicate includes islands through primary wrappers, independently
of their text-member contract. Comma-separated type aliases now share the
statement's declaration chain and all resolve there, rather than detaching
later clauses from the semantic graph. This fixes annotation-only and local
pattern uses as well as later value references; no lazy-reference workaround
remains in MVP.

Boxed null equality retains its proven Bool domain. An inferred homogeneous
scalar match uses the base domain of its literal arms and preserves other
mixed carriers (S11.2.1). Raw element reads now capture snapshots, as existing
map/array reads do (S9.1.2). Record admission distinguishes semantic membership
from the runtime's exact/storage-compatible shape relation; a reordered
literal still reifies into its declared record layout (D4.2.5). Implicit-return
effects survive the body's node-specific producer analysis instead of being
overwritten. Plain deferred scalar stores convert to the destination lane
before boxing. Numeric operations without a native opcode use the existing
boxed operation kernel, including float `div`/`%` under S4.5.3.

`p28-contract-navigation-before.json` records 14 original MVP failures with
14 passing Full controls. `p28-contract-first-unit.json` records the first
10/14 result; `p28-return-match-first-unit.json` passes the remaining four.
The permanent `test_member_contract_representations` passes 90 CLI checks in
`p28-member-contract-gc-final-unit.log`: 14 original fixtures across both JIT
compilers and three GC modes, plus the new member-contract fixture on MVP/T0
across those modes. Full still skips the known int-to-float store conversion
after an earlier deferred failed store, so that control is retained as debt,
not used to weaken the oracle. The initial new fixture used reserved `number`
and mistakenly expected float division to return Error; those authored probe
errors and the interpreter compiler-selection error remain in their logs.
No aggregate/full or performance gate ran.

### 10.75 P29: nullable write places and nested functional loop demand

Write-place admission now classifies the existing normal non-null domain,
just as its element/map checks do, allowing a nullable array alias through the
ordinary checked COW path (S9.1.2). The `for` emitter consumes its caller's
result demand. A braced loop inside functional content still yields a stream
(S2.5.3v3); its source statement spelling cannot discard that requested value
merely because an enclosing conditional is outside direct item position.
Procedural block classification already passes discard demand for statement
loops, so no extra syntax override belongs in the producer (D8.2.6).

`p29-places-query-native-before.json` records 30/32 paired normal checks, with
Full passing all 16 and only these two MVP failures. The original COW/expr
fixtures now pass 12/12 paired GC checks. `p29-functional-place-unit.json`
also retains three Full failures in the pre-existing traversal-continue unit;
MVP passes its three rows. The selected permanent result/place and traversal
Python methods pass 15 CLI checks in `p29-functional-place-python-unit.log`.
No aggregate/full or performance gate ran.

### 10.76 P30: recovery domains and nullable raw helper results

A handler's successful recovery can replace the operand with an unrelated
value (S7.6.1v9). MVP now queries that recovery producer and joins it with the
surviving operand domain, rather than retaining the stale numeric AST lane.
A shared private helper supplies the pass-through domain in planning and
emission. Raw C pointer results now retain nullable transport: a zero returned
by `name()` for null or an anonymous function becomes ItemNull before any
Symbol tag is added (D2.3.5), while existing proven native producers keep their
own descriptors.

`p30-outcome-bridge-before.json` records the 16 selected paired checks: the
two MVP failures and Full's already-recorded deferred float-store defect;
all other numeric/JS bridge controls pass. The focused permanent recovery,
name and empty-symbol unit passes 18 paired CLI checks across normal and
forced/seeded GC in `p30-recovery-pointer-unit.log`. Its 3,545 engine source
hashes match the frozen candidate. No aggregate/full or performance gate ran.

### 10.77 P31: captured writes, admission and task selection

The selected 17-fixture replay recorded six MVP-only failures in
`p31-borrowed-outcome-before.json`. All six now pass, as do the existing
N-D write and row-replacement controls (`p31-borrowed-first-unit.json`, 16/16
paired normal checks).

- Container insertion follows the shared ownership predicate through `if`,
  `or`, `that` and other borrowed-result joins, rather than restricting capture
  to direct member/index ASTs (S9.1.2/S9.3.1). Return capture uses the returned
  expression, not the statement wrapper.
- Semantically compatible array domains still need ordinary value-dependent
  admission when their element contracts differ. Only invariant compatible
  storage proves that check redundant (D4.3.3/S11.4.6v4); a fractional float
  array cannot bypass an int-array parameter boundary.
- A split producer retains Error even when its call AST already describes
  post-caret success (S7.6.3v3). The explicit propagation guard runs before the
  narrowed successful result escapes.
- Captured activations copy immutable array/record access facts into their
  fresh binding state. Discarding these facts left the captured array writer
  with no native witness registers (D8.2.5/D6.3).
- Every axis of a multi-axis write enters the existing checked COW path.
  Keys evaluate once, share the subscript's receiver for `last`, and retain
  root/field/array contracts (S7.2.2/S8.2.4v3/S9.2.2). Coordinate staging for
  N-D reads and Full's direct N-D writes shares fixed activation storage.
- Select's handle/timeout staging moved from Full into one rooted physical
  emitter used by both compilers (S13.1.3v2/S13.1.2v2). S12.3.1 still caps
  calls at 16 source operands; an initial 21-operand probe correctly failed
  both compilers and was corrected, without changing that rule.

The permanent focused method passes 57 CLI checks in
`p31-borrowed-coordinate-fixed-gc-unit.log`: nine fixtures across both JIT
compilers and three GC modes, plus the new projected-coordinate snapshot
fixture on MVP across those modes. That new fixture exposes Full's pre-existing
N-D snapshot mutation and T0's nominal projected-write exclusion; both remain
in `p31-projected-coordinate-fixed-unit.json`. Initial authored record-admission
and excessive-arity probes remain in their logs. Final release
`p31_task_capture_domains` restores the exercised argument-limit policy;
its only other delta from `p31_shared_coordinate_places` is the select ruling
comment. No aggregate/full or performance gate ran.

### 10.78 P32: boundary source identity and inferred binding storage

Full's lazy type-boundary emitter moved into a shared primitive used by MVP.
It retains a pooled label and source offset and resolves line/column only when
admission rejects (S7.4.4). Declaration outcomes use the declaration source;
parameter failures use the source call. The existing two location goldens now
pass unchanged. A pure function named `main` keeps ordinary parameters and
defaults; only a procedure participates in the CLI entry role (S12.1.1v3).

An identical inferred record schema already describes its physical producer,
so it requires no reification. More broadly, an inferred binding selects
storage without admitting a new user contract (D4.3.1). The scanner fixture
first exposed an unplanned inferred-map boundary, then an inferred first
string literal rejecting the next array item. Inferred stores now convert
the planned carrier directly, and the added inferred-array fixture separately
proves that a declared literal refinement still rejects incompatible members
(S11.4.6v4).

`p32-native-result-before.json` records the three original scanner/location
failures with passing Full controls. The first authored main fixture lacked
its statement separator; after correction `p32-named-main-before.json` records
one MVP failure and passing Full/T0 controls. The permanent boundary/entry,
admission-outcome and borrowed/coordinate methods pass 123 CLI checks across
normal and forced/seeded GC in `p32-carrier-boundary-gc-final-unit.log`. All
3,545 engine hashes match `p32_inferred_carriers`. No aggregate/full or
performance gate ran.

### 10.79 P33: native subsystem outcomes and virtual lengths

The paired 16-fixture normal replay recorded 13 MVP failures and no Full
failures (`p33-native-subsystems-before.json`). The unchanged cases cover
archive/ZIP I/O, file/PDF constructors, music, scenes, procedural art and
Markdown edits. These were shared lowering faults, rather than missing
per-package adapters.

Generic and virtual-array `len` now uses the existing checked producer.
Reading a native array header from an archive's virtual content returned a
bogus count. The raw no-GC path remains restricted to strings and proven
primitive rank-one array storage (D2.3.2/D8.2.6). File constructors retain their
Element-or-Error result, including the owning binding's carrier. Conditional
contracts join both actual arm outcomes and condition errors; stale inferred
success-only AST metadata cannot hide an Error alternative (S7.6.3v3/D4.3.1).
An unresolved native namespace value emits the shared Error sentinel, matching
Full's value route without accepting an undefined source identifier.

All 13 failures pass on `p33_native_result_domains`
(`p33-native-subsystems-first-unit.json`). The permanent subsystem, boundary,
admission and borrowed/coordinate units pass **201 CLI checks** across both
compilers and normal/forced/seeded GC in
`p33-native-subsystem-gc-unit.log`. The 3,545 engine source hashes match the
freeze. No aggregate/full or performance gate ran.

### 10.80 P34: retained interpreted entry and native callbacks

The direct native → T0 → native probe exposed a shared runtime gap: the
interpreted call entry required a still-active interpreter runner. MVP and
Full already select that same entry by Function ABI; a retained T0 Function
could therefore fail after its originating runner ended (D7.4.2/D8.3.1).

The cold entry now reuses `InterpStateGuard`, already used by retained host
callbacks. Recursive interpreted calls stay on the active state. Borrowed
transport reads the current evaluator when no walker is active. A recovery
boundary releases retained state before forwarding an escaped fault. It
preserves inner local handlers and the priority of an existing outer
transaction (D7.4.3/D6.3). An invocation without an outer recovery target
publishes the shared durable fault outcome.

The permanent native unit retains an MVP function/callback, loads a T0 relay
in the same evaluator with promotion disabled, and crosses both entry ABIs.
It checks cold floats, Error, containers, precise-root watermarks, explicit GC,
escaped faults, outer transaction cleanup, a local procedural handler and a
later successful callback. Initial authored probes retained an incompatible
compiler selector and then used a pure-function handler instead of a
procedural handler; those failed logs remain available.

The callback and three native MVP view/edit/event units pass **12 checks**
across normal/forced/seeded GC in `p34-retained-final-native-results.json`.
Engine: `p34_retained_t0_completion`; native fixture sources and archive
provenance are recorded in its `retained-entry-final` native manifest.
This closes the remaining inventory probe. P35 starts final verification;
there has been no intermediate Lambda baseline or full sweep in this slice.
Superseded private release executables were removed to free 5.25 GB; hashes,
source freezes, objects, build manifests and failed-test logs remain, as do
the starting control and current releases (`p34-superseded-release-cleanup.json`).

### 10.81 P35: final gate opened residual work

The frozen final candidate and current native harness use release O3 ThinLTO
objects; root executables are untouched. The actual Lambda harness discovers
2,233 cases. A single paired all-case run is progressing in recorded chunks
(`p35-full-language-gate-state.json`); it is not yet complete. Package failures
shared with Full remain visible, including static int/string diagnostics in
procedural-art/chart import cones. Do not turn the old P12 counts into current
results or treat an MVP-only subset as the full language gate.

The Python semantic/emission gate selected 103 methods and recorded 1,985 CLI
invocations. It finished with 100 failed assertions/subtests; 80 CLI exits were
nonzero, including expected negative invocations. Typed correctness is 72/77
MVP and 77/77 Full. Real defects include rejected array/record producers and
closed numeric bodies. Other assertions still expect formerly unsupported
capture/content syntax or pre-S7.7.2v2 declaration failure control. Separate
those stale expectations from runtime defects; keep the original report.

Native security passes 98/98. Native errors passes 307/311. Concurrency ran
93 cases with 48 failures and a suite setup failure. Its process retains a
context after the early Jube/JS cases, so later context-switch failures cascade.
The four focused MVP callback/view/edit/event units pass independently. Native
failure attribution needs focused isolation and controls, not added skips.
Reports are `p35-native-gate-results.json` and the per-executable JSON/logs.

P35 did not run the aggregate Lambda baseline, Radiant baseline or timings.
Residual failures resume focused work before another final gate.

### 10.82 P36 in progress: producer domains and closed scalar locals

Homogeneous literal joins retain a common scalar carrier without imposing a
literal refinement (D4.3.1). Conditional transport keeps the condition's Error
arm, and procedural protected operands retain their successful producer domain
(D2.3.5/S7.6.1v9). These fix the three graph imports and handler result cases.

Open parameter admission preserves the actual's proven normal domain instead
of replacing it with the abstract receiving type. Fill producers publish their
array/Error alternatives and retain certified success domains; inferred fill
bindings keep Error values rather than creating a receiving contract. Generic
matches join their actual arms and possible unmatched absence. Raise's rich
Error constructor participates in effect planning (S7.4.6). These fix noise
imports, retained buffers, Brainfuck, match and non-Error raise units.

Closed numeric alternatives now narrow declared outcomes and inferred scalar
locals under the same proof that checks every initializer/write. Register-class
changes allocate a matching MIR register; checked siblings keep their original
binding plans. Type-preserving math retains its actual operand domain. The
noncollecting assertion remains enabled and now identifies the first offending
helper. Both remaining region rejections named `lambda_type_check_lazy`;
Bounce and Pnpoly now pass without disabling their native alternatives.

`p36_closed_admission_domains` passed **22/24** selected paired checks
(`p36-closed-first-unit.json`). The two remaining failures were CD and
Three-way Merge. The subsequent patches preserve
generic array-image metadata, bounded parameter read domains and constructor
Error alternatives, and delay loop-source queries until every binding is
initialized. `p36_preserved_boundaries` passes **24/24** paired normal checks
(`p36-preserved-first-unit.json`), including all five typed benchmarks rejected
by P35. Its release SHA is
`1e27d9bc8678dcb239187312f92be894a3c1724564c123186bcd7679c0c5b2e0`.
The failed `p36_map_and_binder_domains` build is retained: a patch initially
matched the wrong result join, then was corrected before later builds.

The five focused producer, admission, binder, member and borrowed/coordinate
Python units pass (`p36-transport-gc-units.log`). They include the permanent
`test_producer_carrier_transports` unit (nine fixtures, paired
normal/forced/seeded GC). Next: replay remaining failing package families,
classify P35's residual native failures, and correct obsolete rejection tests
against their formal rulings. The single frozen P35 paired campaign continues;
no additional aggregate baseline/full sweep or performance measurement has
run during P36.

### 10.83 P37: Error-valued admission, mutable counts and callable pipes

An Error-only receiving contract needs the validator's admission status: an
admitted Error has the same Item tag as a rejected normal value. MVP now uses
the existing status-bearing boundary helper for Error-only contracts, as for
dependent admission (S11.4.2v3/S7.7.3). The permanent
`mir_mvp_error_boundaries` fixture covers direct, erased and dynamic calls.
Both citation diagnostic fixtures now pass without changing their goldens.

Static admission no longer proves counted lengths from a binding descriptor:
push/splice can invalidate them without a checked binding write (D4.3.3).
An arbitrary `~`-free pipe RHS now uses the shared callable application helper;
only builder-promoted injected calls use ordinary call lowering. A match arm's
locally bound `~` cannot make that enclosing pipe a mapping (S10.1.7v2).
Boundary diagnostics name source-level parameter, return and store sites. The
shared nested-path adapter selects an array/ND/map diagnostic from its receiver.

Release `p37_boundary_diagnostics`, SHA
`e4bde2c940c01a0cac54f8cbde68bba680adc865c09d5916e4444c254c5034a0`,
passes six focused Python methods (`p37-final-boundary-unit.log`), including
paired normal/forced/seeded GC for Error admission, citation diagnostics,
counted-array rejection and callable-pipe rejection. The three selected native
negative tests pass **3/3 MVP** and **2/3 Full**
(`p37-negative-boundary-results.json`). Full's remaining failures concern a
computed-key Error and an array-fallback diagnostic label; they are retained.

Old Python capture/content rejection assertions became positive checks. The
module-initializer and whole-variable reassignment tests now assert retained
validation Errors and continued execution under S7.7.2v2/S7.7.4v2. Selected old
MVP fixtures also lacked required acknowledgements/return Error alternatives,
or applied `^` to an explicit non-error value. Their source updates make the
intended control explicit. The modular-update golden adds `[true, true]` for
the two retained invalid-index outcomes; no engine rule or expected valid
numeric result was weakened. Failed initial fixture-repair reports remain in
`p37-repaired-fixture-units.json` and
`p37-repaired-contract-fixture-units.json`; shared Full COW differences remain.

The isolated grammar-lint native unit still exits 137 on MVP; its paired Full
CLI control completes in 0.075 s with all warnings and `"compiled"`.
This is an MVP planner defect, not an environmental waiver. Superseded private
executables/archives were removed after disk exhaustion approached; the 47
paths, hashes and sizes are in `p37-superseded-build-cleanup.json`. Current
controls, objects, frozen sources, manifests and test reports remain.

### 10.84 P38: optional call categories and type declarations

Restoring only a call result's contract left optional Bool native returns with
the category of the type-expression descriptor. The shared MVP value builder
now restores the semantic category before native-to-Item conversion (D2.3.5).
`p38_nullable_call_results` passes the array-domain and modular-update paired
units (**4/4**, `p38-nullable-unit.json`).

Grammar definitions have no value initializer. They must bind a type value
(S11.4.12v2), rather than enter inferred mutable-value widening (D4.3.1).
The planner excludes type definitions from value seeding/widening and makes an
already-nullable domain's null join idempotent.
`p38_type_declaration_domains` (SHA
`cb058c766762eea0714d70c3fca056101aa40c1bb32ab3e02e484629a1ef1a35`)
passes the grammar-lint native unit, using the unchanged P37 child-process
harness against the P38 CLI. Its standalone grammar-lint unit completes in
0.86 s; this is diagnostic latency, not a benchmark claim. Selected
grammar/diff/readability/mail units pass 21/22 paired checks; the mail-compose
compiler crash continues into P39. The permanent type-value/nullable-return GC
method passes all 18 CLI checks.

The old float-array fixture now checks that a failed initializer retained
Error and continued (S7.7.2v2); its golden gains `true`. Invalid short-array
kernel calls retain their outcomes instead of propagating out of main
(S7.7.6v2). The shift fixture observes Error codes through the Error contract,
rather than passing Errors into `string` (S7.9.3); its golden adds `true` for
the retained invalid-count result. Both array fixtures pass on MVP and Full;
shift passes on MVP and retains Full's differing poison/count results in
`p38-repaired-array-unit.json`. No goldens were changed to accept Full's COW
or poison discrepancies.

### 10.85 P39: imported bottom and focused residual replays

An Error-only imported return removed its sole alternative to a null pointer.
Its successful domain is `none` (S11.1.7), while the actual Error remains an
ordinary boxed result. The planner now uses its existing normal-domain helper.
The imported module in `mir_mvp_error_boundaries` covers this public ABI.
A release linker map and an isolated crash probe identified the compiler
failure; those diagnostic artifacts are under `temp/`, not runtime changes.

`p39_imported_bottom_domain`, SHA
`0e1e15a5f819f053b3320cf033c0a83f140329dc9317db237994733fca616ecd`,
passes the imported Error fixture and mail-compose control (**4/4**), plus
eight selected diagram/demo fixtures (**16/16**). Two permanent GC units pass
**48 CLI checks** (`p39-bottom-gc-unit.log`). The expansion semantics fixture
also passes MVP; Full's existing differing result remains visible.

The focused replay selected only previously failing source-driven GTest cases
from completed P35 pairs, using the frozen discovery harness and the P39 CLI:
**117/118** pass. Only `scene3d_midtown` remains. A second replay of 21 late
procedure failures passes **16/21**. Logs, selections, executable hashes and
reports are `p39-residual-units.*`, `p39-residual-unit-selection.json` and
`p39-late-residual-unit.*`. These are failure-only units, not another full
verification. The older frozen P35 qualification remains separate and running.

### 10.86 P40/P41 in progress: procedure effects and receiver writes

Planner observation now unwraps expression statements, rather than inheriting
their stale context type. A single print retains null/Error provider outcomes;
this matters when it is a handler's recovery value (S7.6.1v9/S7.9.3).
Direct polymorphic calls check flagged argument colours before entering the
callee, using the same helper as Full (S12.1.4v4), and retain call-site failure
effects. `function_colour_poly`, `record_boundary_admission` and
`verified_contract_prefix` now pass. A proof restriction on value-producing
`for` expressions did not close the range case; it remains unresolved.

`p40_effect_and_statement_domains` (SHA
`0f95a0ade9b075ba94dd5c35689c04151a7b17b07b040d03dbfafa451eb82ef6`)
passes **9/12** paired checks. Full passes all six. The remaining MVP cases are
`dense_for_range` (`fn_to` in a closed region), `scene3d_midtown` (texture
return conversion), and `tune23_array_consumer` (String payload decode after
an open alias appends an integer). Native OS crash evidence confirms the last
case faults while printing a String tagged with address 42. A later temporary
signal-interposition probe itself recursed; its report is diagnostic-tool
failure, not runtime evidence, and is retained separately.

The receiver audit now reuses `ast_body_may_write_entry` for a procedure
method's implicit mutable receiver before preserving an initializer's private
array domain (S9.1.2/D4.3.1). `p41_receiver_and_origins` is building. It also
contains temporary conversion/range origin logs and a linker map for tracing
the other two rejections; remove those logs and `<dlfcn.h>` after diagnosis.
Next: replay only the three remaining fixtures, finish their root fixes and
run their affected unit/GC controls. No new full verification, aggregate
Lambda baseline or performance measurement has run.

### 10.87 P42/P43: local mutation proofs and result carriers

The P41 method audit was necessary but insufficient for the array-consumer
crash: the builder had already lowered `alias.push(42)` to a system call.
The underlying proof shortcut applied parameter escape flags to local aliases.
Restricting it to actual parameters and checking mutation before seeding a
private inferred primitive-array domain fixes the stale string lane
(D4.3.1/S9.1.2). `p42_local_array_proofs` passes that original fixture.

The canonical AST for `fill_range` has a direct `for` body, because the parser
collapses a one-statement block. Function emission now preserves its flagged
procedural discard demand (S2.5.3v3), avoiding an allocating range constructor
inside a closed numeric region. The closed-region collecting-call assertion
remains enabled. A proven union of element outcomes can lack a single source
pointer lane; after proven admission, the receiving contract selects that lane
for Item conversion (D2.3.5). This fixes the imported Midtown texture function.
All temporary origin logging and `<dlfcn.h>` have been removed.

Clean release `p43_admitted_pointer_and_discard`, SHA256
`47e640b025d6eb6b771fc5d51dcf4ab9b8f560cfaf3042117bc77faa7ca2232d`,
passes the three original fixtures on both compilers (**6/6**,
`p43-three-unit.json`). The permanent
`test_procedure_and_pointer_result_contracts` method passes **36/36** CLI
checks: six fixtures on both compilers under normal/forced/seeded GC
(`p43-result-gc-unit.log`). Its new `mir_mvp_pointer_union_admission` fixture
covers both success and recovery element results; the other five fixtures
cover range statements, heterogeneous aliases, callable colour, record
admission and handler prefixes. `git diff --check` passes.

A failure-only replay selects the 11 late MVP-only cases not included in the
P39 replays. Four pass: recursive arrays, nested cancellation, Bounce and
N-Queens. Seven fail (`p43-later-unit.json`):

- `proc_procart_watercolor_sim`: harness timeout, still uninvestigated.
- `pdf_output_streaming`: three recovery/rollback checks produce Error where
  Full produces true. Paired standalone controls confirm the difference.
- EPS `fonts` and `text`: MVP raises the package's composite-typecheck Error;
  paired Full completes successfully.
- EPS `machine` and `raster`: incorrect stack/file semantics, still open.
- `larceny_pnpoly2`: harness status 256, but both standalone runs exit zero
  and match the golden (`inside=29415`). Retain the harness failure pending
  attribution; do not claim stability or benchmark qualification.

The paired direct evidence is `p43-late-paired-diagnostics.json`; these were
correctness diagnostics, not a performance campaign. A native isolation check
using the frozen P35 harness reproduces only the Jube ordinal/husk failure;
callable-realm identity and the first MIR cache unit pass independently
(`p43-p35-native-isolation.json`). Ordinal get/set reject prototype members
before their dead-husk handling, while by-name access handles the husk first.
No Jube change has been made. Broader native cascading failures still need
classification rather than skips.

The original P35 run is now finished: 112/112 compiler chunks, 2,153 paired
reported cases from 2,233 frozen entries; two MVP chunks timed out without a
report. Its input audit records no drift. Historical paired counts are
1,698 MVP / 1,824 Full, with 304 shared, 151 MVP-only and 25 Full-only failures.
Those are old-engine results, not the current residual count. Next resume:
close the six late source cases, attribute the benchmark/native failures and
unreported chunks, then classify remaining Python assertions. Full correctness
and performance qualification remain deferred until known gaps are closed.
The user requested wrap-up at this checkpoint; no baseline or new full
verification was started, and no commit was requested.

### 10.88 P44–P47: explicit output demand, range lanes and live borrows

The resumed tree contained output-demand declarations without matching MVP
implementations. MVP now threads the evaluated PDF destination through pure
local result calls, sequence/control-flow paths and streamable queries, using
the existing call, scope and iteration machinery. Ordinary bound values,
explicit arrays, declared-parameter firewalls, captures and imported calls
retain their value boundaries. Each output expression owns a checked status
exit. A destination-aware handler checkpoints its operand and rewinds partial
bytes before emitting recovery (S2.6.6v3/D8.2.6). Ordinary inlined value calls
clear the caller's output exit, so their propagation stays in their activation.

The EPS bind failure reduced to a counted loop with a boxed
`len(body) - 1` bound. The integer proof selected native range lowering, but
emission compared the Item word directly against integer lanes. Bounds now
retain incoming Errors, convert to their native lanes before guards and carry
a failed range check as the loop outcome (D2.3.5/S8.3.1v3). EPS machine/raster
then pass. Fonts/text still lost heap objects: in `push_operand(m,
lookup_font(m, key)^)`, the later actual replaced `m` after the outer var
argument had read its old owner. The shared write audit now triggers a reread
of the live root or previously evaluated projected place before publishing its
call home. Plain arguments keep their snapshots (S9.1.3v3); place keys are not
evaluated again. This closes fonts/text without editing the EPS package.

Private split debug executable `p47-output-and-borrows.exe`, SHA256
`87f7527353096f9a7715c9223b99bb4fe21753077b67bd54261bcc086a363099`,
qualifies correctness only. All five original PDF/EPS cases pass paired normal
and GC checks. The three new fixtures are `mir_mvp_output_recovery`,
`mir_mvp_boxed_range_bounds` and `mir_mvp_live_borrow_arguments`; their `.txt`
files retain the semantic results. Each fails on frozen P43 and passes on P47
(`p47-regression-controls.json`). All nine MVP regression/GC checks pass
(`p47-gc-final-mvp-unit.log`). The indexed-region, borrowed-coordinate and file
output unit groups pass (`p47-demand-and-region-units.log`). Initial new-fixture
syntax/contract mistakes were corrected in the fixture sources, not engine
rules; failed reports remain under the task directory.

Full passes the live root case but loses the nested place replacement:
`record(parent.child, extend(parent.child)^)` produces `[30]` where `[30, 20]`
is required. MVP preserves the replacement and the saved `[30]` snapshot.
All three Full GC controls expose this same difference
(`p47-live-borrow-final-unit.json`, `p47-borrow-regression-units.log`). No golden
or test was weakened to accept it. This is a Full-only follow-up, not an MVP
failure or a qualified paired all-case pass.

### 10.89 P48–P50: native husks, precise test roots and residual isolation

Ordinal get/set checked prototype placement before the dead-wrapper rule.
By-name operations handled the dead wrapper first. The ordinal path now
matches that ordering and never touches the discarded payload (D9.5.4).
The existing parity fixture had three independent defects: its wrappers/keys
were unrooted across allocations (D6.3.3); it compared Lambda Set's value with
ordinal JS Set's Boolean completion; and it assumed unique names per ordinal,
although the interface compiler deliberately emits receiver-specific binding
variants. It now roots four live values, compares the JS Set surface, and
checks every variant's name round trip and guarded operations. No member or
operation is skipped. The failed assertion had bypassed runtime cleanup,
causing later realm/cache failures in that process.

Private native executable `p50-concurrency-units.exe`, SHA256
`bb4f6b14d23ecd6e7e73009613fa148d4db139ec32e60b507f70d503aa574153`,
passes seven focused units in normal, forced and seeded GC (**21/21**):
husk parity, realm identity, fresh-runtime MIR cache, and the four MVP retained
view/callback/edit/event cases (`p50-native-unit-results.json`). A failure-only
replay of 37 P35 cases passes **35/37** (`p50-native-residual-units.json`).
Both remaining document tests abort in Apple's Objective-C post-fork guard,
before their semantic assertions. They pass **2/2** with
`--gtest_death_test_style=threadsafe`, which starts fresh child processes
(`p50-document-fresh-process-units.json`). No Objective-C guard was disabled,
and the default-style failures remain recorded.

Native build verification first encountered stale generated link names, then
the relocated executable's node-core rpath. Regenerating through the current
Premake generator fixes the former; pointing `DYLD_LIBRARY_PATH` at the real
checkout module supports the private executable's new location. Neither is a
runtime workaround or a sibling-source change. The original failures and the
subsequent loaded reports are retained. No aggregate native gate ran.

### 10.90 Watercolor correctness and remaining qualification

A small sheet/stamp/step probe passes on both compilers
(`p47-watercolor-step.json`). The original watercolor script still exceeds the
45-second focused diagnostic timeout on the debug MVP host
(`p47-watercolor-focused.json`). A stage-marked diagnostic progresses through
all ten sections and matches the expected physical results. A subsequent run
of the unchanged source completes and matches its golden with a 300-second
diagnostic limit (`p47-watercolor-completion-unit.json`). This establishes
output correctness, not the ordinary harness timeout or release performance.
Do not increase the checked-in timeout or call this benchmark qualified.

Next work remains release-build timeout investigation, the Full-only projected
borrow discrepancy, historical unreported chunks and the remaining Python
semantic/emission assertion classification. Full correctness, the Lambda
baseline and paired typed/untyped performance remain deferred under the user's
verification policy. The task's exact source/binary hashes are recorded in
`temp/mvp-parity-resume/p50-checkpoint-provenance.json`. Changes remain
uncommitted; no commit was requested in this continuation.

### 10.91 Untyped release tuning (2026-11-09)

The user requested faster untyped execution across the registered 77 cases,
with no regression in typed MVP. This performance task preserves P44–P50 and
does not restart the deferred language/baseline campaign. Measurements use
the scripts' reported workload times, excluding process startup and compilation;
all outputs must match their existing goldens. No workload, timer or benchmark
golden was changed. Competing builds/tests cause whole paired rounds to be
discarded and retried.

The split release build produces private executables, leaving root executables
unchanged. The frozen starting release SHA256 is
`e45cec36fef54f4ebb1d2833fbde9d99dbc79f4c8b6be926630025f5cfb5ad70`.
Its source identity, starting dirty diff, untracked fixtures and sibling
revision/status records are archived under
`temp/mvp_untyped_tuning_20261109/`. The benchmark fingerprint now resolves
split-owned source paths through `utils/split_paths.py` (**D9.1.1v4**).
The initial 3-pair screen passes all 77 untyped goldens, with 30/77 faster
Full medians and geometric MVP/Full ratio 1.07515. This is screening evidence,
not an every-row performance qualification.

The shared changes address five causes:

- Boxed array iterations can now enter existing closed numeric regions after
  exact physical int/float/bool storage, extent and COW guards. The region proof
  still rejects replacement, retaining/reentrant calls and collecting stores.
  Unknown map-member setters are rejected explicitly: their effects had no
  proof in this array region. Guard misses execute the original body before
  any source effect (**D4.3.1**, **D5.4.4**, **S9.1.2**).
- Native range selection uses current producer domains. Range counter lane
  hypotheses widen with the existing call/write fixed point, so an untyped
  bound inferred to remain integer avoids generic iterator dispatch; conflicting
  callers and later replacement preserve generic iteration. Boxed successful
  endpoints still convert through their actual lanes (**D2.3.5**,
  **S8.3.1v3**).
- Dynamic scalar operators try exact float carriers after the existing int
  path, reusing shared numeric emission. Null, Error, mixed numeric carriers
  and other types keep shared boxed helpers (**S4.3**, **S7.9.3**).
- Confined recursive mutable int/bool parameters can retain a private array
  observation and the existing scalar snapshot. Every incoming value and
  indexed write must preserve its exact element domain; widening discards
  the private lane and snapshot. This adds no inferred admission or store
  contract (**D4.3.1**, **S9.1.2**, **S12.2.1v3**).
- Erased Boolean arrays cache their physical witness beside int/float ones.
  Guarded branch demand reads a byte directly; absence is falsy and every
  other carrier uses ordinary truth/error handling. Shared-home reentry and
  new inline activations invalidate the witness (**S3.1v2**, **S7.1.1v3**).

Permanent fixtures are `mir_mvp_erased_iterations`, `mir_mvp_untyped_ranges`
and `mir_mvp_untyped_snapshots`; `mir_mvp_scalar_fallback` adds erased float
arithmetic. Each has an expected result. Focused checks exercise ordinary,
forced and seeded GC, nullable/alternate storage, COW snapshots, short arrays,
map writes, inference widening and recursive transport. Eleven focused methods
pass all MVP checks. Three paired Full-only subtests fail on the recursive
string replacement, also reproduced with the frozen starting Full engine;
the correct golden remains, and **LR07-53** records the defect. The temporary
proof tracing was removed before measured candidates were built.

P7 completes 77/77 untyped goldens, with 36/77 faster Full medians and
geometric MVP/Full ratio 0.88188. P8 refines float-guard operands to a non-null
domain before native comparison emission; the scalar fixture exercises a
nullable float against an erased operand. P8 is the current release (SHA256
`145aa48df4a1bc3af7fd1f3df3128c2e137b85c09ad0d40fd961177c4db84dab`).
Its focused unit summary records no MVP failures and the three known Full-only
LR07-53 subtests. P8's complete five-pair typed screen passes all 77 goldens
and has geometric candidate/starting-MVP ratio 0.99847 over 76 nonzero timer
rows; canonical Sumfp reports zero for both. Four rows are selected for longer
confirmation: Queens, Brainfuck, Splay and SHA-1. The complete current untyped
screen has finished; its final figures are recorded below. Reports and source/binary manifests are under the
task artifact directory.
The P2 hotspot comparison already established N-body at 6.001 ms versus Full
10.498 ms and prior MVP 111.797 ms, and FFT at 0.247 / 0.249 / 1.889 ms,
using five interleaved measured triples. Later candidates supersede those
numbers for final qualification; slow families and per-row typed regressions
remain open until the current reports and confirmation runs are reviewed.

The remaining optimization directions are shared producer/transport causes,
not workload substitutions: close recursive array/result domains without losing
absence/error semantics, hoist stable erased-array checks across whole counted
loops, and extend physical record-candidate access using the existing shared
shape/descriptor helpers. Record speculation must retain an exact live layout
guard and boxed semantic domain (**D7.4.1**, **D2.6.2**, **D4.3.1**). These are
next steps; this batch does not implement or qualify them.

P8's final untyped campaign passes all 77 goldens: **35/77** faster Full
medians, **42/77** slower, geometric MVP/Full ratio **0.88121**. The frozen
starting engine was 30/77 faster and ratio 1.07515. This is approximately an
18% improvement in normalized MVP time. Current Full comparisons include
N-body 5.490/9.737 ms, Kostya primes 5.005/11.764 ms and Larceny primes
4.892/11.410 ms. FFT is near parity at 0.230/0.236 ms. These complete-screen
values use three measured pairs; they do not establish every-row qualification.
`p8-summary.json` retains every slower row and both engines' failures (none).

The five-pair typed screen's 0.99847 geometric ratio is essentially unchanged.
Four 15-pair confirmations clear Queens, Splay and SHA-1, but confirm typed
Brainfuck at 1.03441 times the starting MVP. MIR comparison shows a new general
float/int array alternative ahead of the existing fused modular store. P9
extracts the existing modular-store planner and gives a sole matching store
its original native path; no benchmark name or source workaround is used.
The modular fixture now checks conditional erased stores, floating misses and
GC snapshots, and rejects a preceding `fmod`/slow-int alternative in MIR.
This preserves saturation, bounds and COW rules (**S4.1.2**, **S9.1.2**).

P9 release SHA256:
`761ee4f35fd6053d131fe3fa06bb5153692d9edb785814540bdad6b4df1d3423`.
Fifteen paired typed runs against the starting MVP give Brainfuck ratio
**0.98251** and Fasta **0.98187**, neither statistically slower. Fasta is the
other benchmark matching the changed modular-store pattern; the broad P8
screen is not relabeled as a P9 full rerun. P9's three focused methods and the
paired three-GC-mode modular fixture pass. Affected untyped Brainfuck/Fasta
comparisons are retained separately in `p9-untyped-*.json`.

No confirmed typed slowdown remains in these checks. The every-untyped-row
Full-MIR target remains open. No Lambda/Radiant baseline, C2MIR run or final
full-language gate was run in this tuning batch. Changes are uncommitted.

Final scope checks retain the individual engine identities. P9's five-pair
untyped Brainfuck comparison is 246.520 ms versus frozen Full 247.907 ms and
P8 MVP 259.275 ms; its ratio bound still crosses parity with Full. Untyped
Fasta is 0.854 / 0.774 / 0.798 ms; the apparent increase versus P8 is not
statistically significant in this small screen, and Fasta remains slower Full.
These rows are not silently substituted into the homogeneous P8 full-screen
count. `final-summary.json` records the P8/P9 distinction and the remaining
42 slower P8 rows. All compiler source and executable hashes were rechecked
after measurements. Sibling source trees and root executables are unchanged;
no benchmark campaign remains running.

## 10.92 Inferred record read tuning (2026-11-09)

Resume from Lambda `1540d9228` after Radiant `558eb3e` resolves the property
archive blocker RI-003 (**D9.1.1v4 / D9.1.3v2**). The frozen current-source
release is `temp/mvp_untyped_tuning_next/starting-release.exe`, SHA256
`cda6b0f4985ac9e440494d8b86badbbbe2c7d386e86e6483e1e45b7a4fb14363`.
It supplies both the Full MIR control and the prior-MVP control; neither root
executable is replaced. All receipts below are under that artifact directory.

P10 extends the existing guarded member read to inferred construction layouts.
The registry supplies a unique field-name candidate when no contract proves a
field. Each inferred read checks the receiver kind and exact live TypeMap,
then calls the shared `map_shape_field_to_item` decoder. This handles unaligned
nine-byte TypedItems and null/bool slots retagged without changing shape
identity (**D2.6.2 / D3.4.1**). A mismatch retains the ordinary member helper;
the candidate never supplies a semantic contract (**D4.3.1 / D7.4.1**).
The existing Full MIR addressability helper moves to the shared shape header;
its condition and Full MIR callers are unchanged.

P11 propagates compile-time candidates through bindings, direct arguments and
returns, using the existing shared shape traversal. The first construction
candidate wins, so recursive graphs converge without runtime feedback or a
mutable per-site cache (**D7.4.1**). P12 uses the destination-selected layout
when a literal is constructed under a declared record contract (**D4.2.5**).
Declared record loads retain their existing native lanes and reusable guards.
Runtime helpers, value layouts and benchmark sources/goldens are unchanged.

The permanent `mir_mvp_guarded_record_reads` case covers packed fields, multiple
construction layouts, missing fields, null receivers, retained snapshots and
live null/bool retags, plus returned i64/u64 ownership across later construction.
It and the existing inferred-read/retag fixtures pass
under both compilers with normal, forced and seeded GC; MIR checks require
both the descriptor arm and generic fallback. The current COW emission check
now asserts its combined header-word predicate, replacing a stale byte-load
assertion that also failed on the frozen starting engine.

P10/P11 five-pair focused screens cover twelve previously slower workloads.
P11 reduces List from 0.540 to 0.320 ms, AWFY DeltaBlue from 135.587 to
92.299 ms, and Hashmap from 109.353 to 76.077 ms. These remain slower than
Full MIR and are preliminary screens, not a replacement for a homogeneous
77-case campaign. Raw samples, complete goldens and binary/source manifests
are retained for each candidate.

P12's twelve selected unit methods have no MVP failures. Three paired Full-only
subtests retain the known recursive string-replacement failure **LR07-53**;
the correct golden remains unchanged. The complete release untyped comparison
passes **77/77** goldens across all three measured engines. All 77 typed goldens
pass as well. No Lambda/Radiant baseline,
C2MIR or full-language qualification is part of this performance batch.

P12 release SHA256:
`b1e56d966cbe104fb27484f761608456ca49e7eebb3ed39fea2337ff5582771d`.
The homogeneous three-pair untyped campaign has geometric candidate/prior-MVP
ratio **0.95626** (4.4% less execution time) and candidate/Full ratio **0.84347**
(15.7% less). Both the prior MVP and P12 have **35 faster / 42 slower** Full
medians in this same campaign. The every-row target remains open. The report
is `p12-untyped-all.json`; JIT compilation and process startup are excluded.

Selected medians from that complete campaign (milliseconds):

| Untyped workload | Prior MVP | P12 MVP | Full MIR |
|---|---:|---:|---:|
| AWFY List | 0.571 | 0.327 | 0.233 |
| AWFY DeltaBlue | 137.087 | 93.416 | 73.498 |
| JetStream Hashmap | 112.805 | 77.835 | 67.012 |
| JetStream Raytrace3D | 60.536 | 37.350 | 63.633 |
| AWFY Richards | 595.730 | 482.770 | 284.034 |
| Text jq_bf | 287.848 | 229.435 | 138.120 |

The five-pair typed campaign has geometric candidate/prior-MVP ratio
**0.99844** over 76 nonzero rows, with no statistically confirmed slowdown.
Twelve apparent median increases above 2% are rechecked with 15 measured pairs
and three warm-ups each; none is confirmed slower. Canonical Sumfp reports zero
on both engines. A separate matched 100,000-iteration Sumfp batch also has no
confirmed slowdown; it does not replace the canonical row. Raw screen and
confirmation samples remain separate in `p12-typed-all.json` and
`p12-typed-confirm-*.json`.

`p12-summary.json` records all 42 slower untyped rows, both geometric
comparisons, typed follow-ups and verified executable/source/corpus hashes.
The final wide-scalar record fixture and updated COW guard assertion pass;
the assertion also passes on the frozen control. The selected unit campaign
retains only the three known Full LR07-53 subtest failures. Benchmark algorithms,
sources, goldens and the timing harness are unchanged. No campaign
remains running; changes are uncommitted.


## 10.93 Guarded scalar bodies and open-container transport (2026-11-09)

Starting tree: clean master `d3582e267`. The P12 source manifest matches all
measured runtime sources except the merged test-target build configuration.
P12 remains the frozen Full MIR and prior-MVP control; `starting-identity.json`
records the difference. Artifacts are under `temp/mvp_untyped_tuning_p13/`.

P13–P15 generalize the existing construction-candidate traversal to scalar
predictions. Terminal primary nodes are literal evidence, not empty wrappers;
array-index depth distinguishes a selected element from its array owner.
Private closed calls can keep exact string/Boolean domains as well as numeric
lanes. Erased parameters can select a guarded body: all exact physical type
checks precede body effects; a miss enters the original body once. The native
alternative retains the original callable ABI/effects and widens every local
initializer/assignment. Each changed representation gets an independent MIR
register and precise root class, including Item-to-pointer changes that share
the same machine register width (**D7.4.1 / D4.3.1 / D6.3**).

P15's focused five-pair untyped screen passes all seven workload goldens. Ray
improves from 2.721 to 0.598 ms versus Full 0.677 ms; Fast Diff from 488.669 to
88.230 ms versus Full 169.715 ms. Levenshtein is 26.938 versus Full 27.013 ms;
that small margin still needs a complete campaign and longer confirmation.
These are selected results, not an every-row qualification.

P16 fixes open record writes: the implicit no-Error parameter admission is an
entry filter, not a record field contract. Routing it through transactional
map admission cloned a map on each update. Open writes now use ordinary COW
and the existing resolved-field setter, which checks the prepared owner's
layout and retains canonical retag/capture semantics (**S9.1.3 / S12.2.1v3 /
D4.3.1**). The permanent fixture covers snapshots, ordinary updates, layout
changes and Error data. P16's eight-case focused screen reduces Permute time
by 51%, Storage/Towers by 33%, Richards by 28% and Splay by 25%; several still
remain slower than Full.

P17–P18 add an inferred integer-field store only after exact shape, COW,
live descriptor/flags and finite-value guards. Retags and shared owners retain
the canonical setter (**D2.6.2 / D4.3.1**). Boxed arrays can reuse an entry's
physical storage witness for reads and length without receiving an element
contract. The guard excludes views, N-D, native pointer lanes, JS storage and
virtual lists; the existing shared physical-array guard now supplies both
read and witness sites (**D2.3.4 / D4.3.3 / S7.1.1v3**). Cached lengths follow
the existing witness refresh/invalidation paths.

P19 places the boxed read after numeric alternatives: putting it first had
added a branch to every ArrayNum iteration. Its focused screen restores
numeric performance and measures Permute 0.135 versus Full 0.264 ms, Storage
0.312 versus 0.394 ms, and Hashmap 63.601 versus 65.349 ms. P20 uses the same
live-descriptor guard for inferred integer reads, preserving nullable/poison
transport rather than asserting a field contract. P21 excludes parameters
and locals with indexed/member writes from speculative scalar narrowing;
their generic setters must continue to receive tagged Items, including invalid
string/scalar writes that yield Error (**D4.3.1 / D2.6.2**).

P22 also clears the boxed witness alongside numeric/record witnesses when a
captured home reloads after a collecting/reentrant call. The boxed-array
fixture covers parent-fn rebinding of a precisely rooted home shared with a
confined pn, within **S9.1.4v2 / D6.3** permissions.

P22 passes 11 focused unit methods, including three new fixture/golden pairs
under both compilers and normal/forced/seeded GC. The private release has
SHA256 `ec19808d13a2915d322dd5741edf9c24609ce6aef71fc9f4425fcebb2ec9a833`;
`p22-manifest.json` freezes its compiler identity. The P18/P20/P21 corpus
screens were interrupted for the branch-order, scalar-write and captured-home
corrections; their status files explicitly mark them incomplete. Final
homogeneous P22 comparisons use reported execution time, excluding startup
and JIT compilation. Benchmark algorithms, sources, goldens and the timing
harness are unchanged. No Lambda/Radiant baseline, C2MIR or full-language gate
is part of this batch.

The complete untyped screen uses three interleaved measured rounds and one
warmup per engine. All 77 goldens pass under candidate MVP, frozen Full MIR
and frozen prior MVP. Candidate medians beat Full on **47/77**, with **30/77
slower** and no ties. The prior MVP beats Full on 35/77 in the same campaign.
The geometric candidate/Full ratio is **0.72208** (prior/Full **0.84417**);
candidate/prior is **0.85538**, or 14.5% less execution time. These are median
screen results; narrow per-case margins are not confidence-qualified wins.

| Untyped case | P22 MVP ms | Full MIR ms | Prior MVP ms |
|---|---:|---:|---:|
| Fast Diff | 90.662 | 178.775 | 512.991 |
| Text Search | 6339.890 | 7808.630 | 11814.200 |
| AWFY JSON | 4.761 | 5.337 | 11.105 |
| K-Nucleotide | 0.738 | 0.817 | 1.818 |
| Collision Detection | 425.054 | 490.566 | 515.920 |
| Hashmap | 62.669 | 67.063 | 77.340 |

The typed screen uses five pairs and one warmup. All 77 goldens pass; the
geometric candidate/prior ratio is **1.00382** over 76 nonzero rows. Its four
initial statistical slowdown flags and every median over 1.02 receive
15-pair, three-warmup checks; Sumfp receives a separate matched batch because
its canonical timer is below resolution. All 16 checks pass their goldens.
The original four flags do not repeat; K-Nucleotide and Splay produce new
flags. K-Nucleotide does not repeat independently (0.98947; same-binary
control 1.00702), while Splay repeats at 1.02654. P22 does not close typed
performance qualification; its Splay follow-up drives the next correction.
The every-row Full target remains open, especially Puzzle (3.262× Full),
JetStream DeltaBlue (2.214×), Collatz (2.091×), Paraffins (1.753×), GCbench
(1.606×) and the remaining recursive/record/text cases. Final machine-readable
results, every remaining slower row and provenance checks belong in
`temp/mvp_untyped_tuning_p13/p22-summary.json`.

## 10.94 Candidate layouts across declared returns (2026-11-09)

Splay's writable inferred `node` intentionally has an open write domain
(**S12.2.1v3 / D4.3.1**). Its candidate came from a map literal inside a
function whose declared result is `SplayNode`. Return admission can replace
that literal's layout with a prepared declared layout (**D4.2.5**). The old
candidate thus predicted the pre-admission shape. P22 added six resolved
setter calls to typed `splay_insert` on that prediction. The existing canonical
setter handles prepared layouts; the resolved helper requires exact shape
identity to reuse the supplied field.

P23/P24 tests of candidate trust and binding contract did not remove those
calls: the hint was untrusted and the writable local's contract was erased.
The diagnostic image confirms both facts. These diagnostic images are not
final performance evidence, and all temporary logging has been removed.

P25/P26 fix prediction at the producing boundary. For a local initialized by
a direct call with a concrete declared map result, use the addressable
declared shape or decline the hint; do not search the producer's literal or
fall back to an unrelated globally unique shape. Generic `map`/`object`
results do not prescribe a concrete layout and retain ordinary prediction.
Resolved writes remain for untrusted construction shapes; declared prefixes
keep their existing canonical fallback (**D7.4.1 / D4.3.1**).

Typed Splay's emitted MIR now matches P12 after address normalization, with
the six original generic setter calls. The permanent `named_copy` case
checks the typed producer, snapshot detachment, retained values under forced
GC, and absence of the inappropriate resolved setter. P25's first 15-pair
Splay check reduces the reproduced slowdown to a still-flagged 1.00718;
identical MIR alone does not qualify performance.

P26 passes 11 focused unit methods and freezes SHA256
`39edeb116459fadca1ed71b97461c96a86ef4714a8ea331bdb4227d8d0d19ef7` in
`p26-manifest.json`. Its 15-pair Splay check is 515.946 versus prior 513.619 ms
(ratio 1.00453; two-sided 95% bounds 0.99840–1.01095, no statistical slowdown).
Final qualification runs that Splay check, all 77
typed rows (five pairs), all 77 untyped rows (three rounds with Full/prior
controls), then 15-pair checks of flagged/>2% typed rows. Splay and
K-Nucleotide are included even if the new screen does not flag them; Sumfp
uses a matched batch. The final source is assessed from these receipts, not
P22's complete screen or the interrupted P18/P20/P21 diagnostics.

The final P26 untyped campaign passes all 77 goldens for candidate MVP, frozen
Full MIR and prior MVP. Candidate medians beat Full on **48/77**, with **29/77
slower** and no ties. Prior MVP beats Full on **37/77 in this same campaign**;
the earlier P22 campaign's 35/77 control count is a separate timing sample.
The geometric candidate/Full ratio is **0.71611**, prior/Full **0.84433**, and
candidate/prior **0.84814**: 28.4% less execution time than Full overall and
15.2% less than prior MVP. Thirty-one rows are statistically faster than prior
MVP. Median counts include narrow margins and are not every-row confidence
qualification.

| Final untyped case | P26 MVP ms | Full MIR ms | Prior MVP ms |
|---|---:|---:|---:|
| Ray | 0.655 | 0.700 | 2.957 |
| Permute | 0.116 | 0.273 | 0.749 |
| Storage | 0.344 | 0.434 | 0.679 |
| Fast Diff | 90.162 | 172.892 | 505.712 |
| Text Search | 6219.220 | 7788.940 | 11682.100 |

All 77 typed goldens also pass. The geometric candidate/prior ratio is
**0.99861** over 76 nonzero canonical rows; Sumfp remains below canonical timer
resolution and has a separate matched batch. The four initial statistical
flags (Fibfp, Triangl, Splay and Crypto SHA1) do not repeat in their 15-pair
checks. All 15 focused follow-ups pass their goldens. Their only new flag is
Primes at 1.02125. An independent **30-pair** check is 0.844 versus prior
0.8475 ms (ratio **0.99587**, two-sided 95% bounds 0.96644–1.01422, no slowdown
flag); its normalized MIR also matches P12. No typed slowdown reproduces.
All original screen/follow-up flags remain in the receipts.

`p26-summary.json` records every remaining slower row, all follow-ups and the
independent recheck. Candidate/control SHA256, current compiler-source
identity and all measured benchmark/golden hashes match. `git diff --check`
passes. No benchmark algorithm, source, golden or timing harness changed;
three new unit fixture/golden pairs were added. No Lambda/Radiant baseline,
C2MIR or full-language verification ran. The **29 remaining slower untyped
rows still fail the requested every-row Full MIR target**; overall speed and
typed preservation do not close that target.

### 10.95 P27–P36: boxed finite loops and inferred array consumers

This continuation starts from clean `84f18b39b`. Its compiler-source corpus
matches frozen P26, which supplies both Full MIR and prior-MVP controls.
Artifacts are under `temp/mvp_untyped_tuning_p27/`.

The planner now preserves audited primitive-array seeds through scalar
producer seeding. Immutable integer initializers and closed incoming arguments
can supply demand-only intervals before final range publication. These queries
use the existing incoming-call helper, bounded recursion and shared AST
identities; they publish no second type/fact table (**D8.2.5**).

The finite numeric-loop proof accepts guarded, unannotated boxed integer/Error
locals. The new native registers have independent root classifications
(**D6.3 / D2.3.5**). Entry guards run before effects. Normal completion and
radius misses publish finite values once; a miss resumes the original checked
loop at its current state. An entry miss skips publication entirely. Locals
declared inside the loop cannot supply initialized entry values. Four source
iterations retain their original tests, breaks and statement order
(**D4.3.1 / D7.4.1 / S7.7.2v2**).

Guarded scalar bodies now accept integer construction hints and integer
consumer predictions. Exact Item guards precede all body effects, while float,
null, invalid and wide alternatives retain the original body and ABI. The
new opaque-input case tests predictions without a constructor hint. No
unconditional bitwise-int inference is retained: existing boxed poison/Error
behavior remains in the checked helper path (**S4.1.2 / D4.3.1**).

P33's partial performance screen exposed a Primes regression: a private
`bool[] | Error` observation blocked the native store version used for erased
owners. P35/P36 use the same physical owner/lane/COW guards for boxed array
outcomes, sharing owner guards between iteration and strided-loop emitters.
Inferred owners also retain ordinary constructors, avoiding certificate
maintenance on raw writes. Private observations add no receiving schema
(**D4.3.1 / S12.2.1v3**). P33 is superseded diagnostic evidence, not a complete
qualification run.

Frozen P36 SHA256 is
`c417537683b6ec9d768d1133fddefc1678098f49bbd072bae6f846f6040511e6`.
Fourteen focused unit methods pass, including normal/forced/seeded GC and two
new fixture/golden pairs. They cover entry misses, radius resumption, normal
completion, early breaks, snapshots, open writes, opaque scalar domains and
native stores into boxed array outcomes. The array fixture also reproduces
Full's pre-existing nonrecursive var-owner replacement mismatch on P26;
LR07-53 records the evidence. Its correct golden remains unchanged, and that
fixture's complete oracle is checked on MVP.

The complete untyped screen passes all 77 goldens for candidate MVP, frozen
P26 Full and prior P26 MVP. Candidate medians beat Full on **48/77** rows,
versus **47/77** for prior MVP in the same paired campaign. The geometric
MVP/Full ratio is **0.67503** (prior MVP/Full **0.72332**); MVP/prior is
**0.93323**, a 6.7% reduction in geometric mean execution time. Three measured
rounds follow one warmup; every raw sample and golden/source hash is retained.
These are script-reported execution times, excluding startup and JIT, with
the runner's per-case workload boundaries unchanged.

| Untyped gain | P36 MVP ms | Full MIR ms | P26 MVP ms |
|---|---:|---:|---:|
| kostya/collatz | 149.342 | 268.644 | 565.195 |
| larceny/puzzle | 3.605 | 4.243 | 13.538 |
| kostya/primes | 2.559 | 11.816 | 5.215 |
| larceny/primes | 2.545 | 11.876 | 5.099 |
| text/fast_diff | 73.007 | 190.458 | 90.425 |

The complete typed regression screen passes all 77 goldens for candidate and
prior MVP. Five pairs follow one warmup. The geometric MVP/prior ratio is
**1.00349** over 76 nonzero rows. Seventeen 15-pair, three-warmup follow-ups
cover every statistical flag or median increase above 2%, plus matched
100,000-kernel Sumfp batching for its zero-resolution screen. None confirms a
slowdown. Independent 30-pair Array1/Sieve checks likewise have no statistical
slowdown flag; both also emit identical MIR after process-address
normalization. Seven equivalent untyped follow-ups have no slowdown flags.
Original screen flags remain in the receipts; follow-up classification does
not rewrite the homogeneous campaign's medians.

All eight provenance/release checks pass in `p36-summary.json`: executable
hashes, current and measured compiler sources, archived manifest identity,
benchmark/golden corpus, runner sources and the release marker. The unit log
is `p36-focused-units.log`; complete campaign receipts are
`p36-untyped-all.json` and `p36-typed-all.json`. No Lambda/Radiant baseline,
C2MIR or full-language gate ran in this continuation.

The every-row Full target remains **open**. These 29 rows have slower Full
medians in the complete screen; small differences remain timing estimates,
not a claim of statistical separation:

| Remaining untyped row | MVP ms | Full MIR ms | MVP/Full |
|---|---:|---:|---:|
| jetstream/deltablue | 63.254 | 28.362 | 2.230 |
| larceny/paraffins | 0.365 | 0.207 | 1.763 |
| larceny/gcbench | 158.180 | 98.081 | 1.613 |
| awfy/queens | 0.275 | 0.183 | 1.503 |
| text/jq_bf | 206.475 | 141.195 | 1.462 |
| awfy/list | 0.339 | 0.237 | 1.430 |
| beng/binarytrees | 5.828 | 4.114 | 1.417 |
| awfy/sieve | 0.034 | 0.025 | 1.360 |
| r7rs/nqueens | 1.066 | 0.788 | 1.353 |
| text/three_way_merge | 2232.640 | 1813.130 | 1.231 |
| text/prettier_ast | 801.032 | 652.734 | 1.227 |
| awfy/towers | 0.618 | 0.512 | 1.207 |
| awfy/deltablue | 89.773 | 75.072 | 1.196 |
| text/log_pipeline | 4366.460 | 3695.570 | 1.182 |
| text/jq_tree | 1654.810 | 1490.040 | 1.111 |
| jetstream/splay | 232.472 | 210.564 | 1.104 |
| jetstream/richards | 317.292 | 288.343 | 1.100 |
| beng/pidigits | 0.336 | 0.306 | 1.098 |
| awfy/richards | 323.719 | 295.411 | 1.096 |
| r7rs/mbrot | 0.646 | 0.592 | 1.091 |
| beng/fasta | 0.883 | 0.813 | 1.086 |
| text/microdiff | 32.986 | 30.672 | 1.075 |
| larceny/deriv | 16.861 | 16.019 | 1.053 |
| larceny/pnpoly | 1.946 | 1.852 | 1.051 |
| kostya/base64 | 134.883 | 128.966 | 1.046 |
| text/jq_records | 310.969 | 302.337 | 1.029 |
| kostya/levenshtein | 28.219 | 27.695 | 1.019 |
| kostya/brainfuck | 258.814 | 256.835 | 1.008 |
| awfy/havlak | 4966.950 | 4953.930 | 1.003 |

## 10.96 Batched untyped performance goal (2026-11-09, in progress)

The user explicitly starts a goal to remove every untyped slowdown, preserving
typed MVP performance. The later instruction defers validation until **more than
ten** previously slower cases become faster than Full. Focused release timing
probes retain the runner's existing golden sanity checks; no unit suite,
aggregate campaign or baseline runs before that gate. Count gains on one frozen
candidate, rather than accumulating wins from superseded binaries. Starting HEAD
is `84f18b39b` with the uncommitted P36 slice retained. Full uses frozen P26;
previous MVP uses frozen P36. All new receipts live under
`temp/mvp_untyped_tuning_goal/`.

Shared compiler changes address observed overhead without changing contracts or
data layouts:

- Immutable activation-owned scalar homes can serve repeated reads; mutable,
  captured, borrowed and repeated loop declarations keep separate snapshots
  (**D6.2.2–D6.2.3 / D6.3 / S12.2.1v3**).
- Implicit `any \ error` admission checks the Error tag directly and preserves
  its payload and existing failure destination (**S11.4.3 / S7.7.3**).
- Small boxed-result functions can inline with a defined result on every error
  exit. Larger collecting open/map mutators retain calls; broad expansion had
  regressed Splay in P42 (**D6.2.2 / D7.4.1**).
- Inferred record reads guard the live descriptor and may try two additional
  static layouts with the same keys. Packed `any` slots directly decode null
  and ordinary container pointers; scalar and raw-container reification retain
  the canonical decoder. The existing unaligned-load primitive is shared with
  Full, rather than duplicated (**D4.3.1 / D2.6.2 / D7.4.1**).
- Static map allocations already own their data buffer. Ordered literal fields
  use the audited NO_GC descriptor setters, removing the unnecessary collecting
  varargs fill call (**D6.3**).
- MVP shares Full's literal string equality primitive, preserving Error identity
  and cross-family results (**S5.1.1v2**). Erased bool/float array stores use live
  physical-lane and writable-COW guards, preserving ordinary widening fallback
  (**D4.3.1 / S9.1.2**).
- Present one-character ASCII slices reuse the initialized runtime character
  table. Negative/clamped/empty, Unicode, absent and alternate-receiver cases
  retain `fn_slice3` (**S7.1.2**).

P44 crosses GCbench and List below Full; P45 crosses Prettier and Log Pipeline.
P46 freezes these changes plus primitive stores and character slices, with
SHA256 `a5e9c19d1d8e9047710425abbf1dfa359c55c886ebaa1756cc55e43797ca706e`.
Fifteen focused five-pair timing probes and 15-pair Queens/Nqueens follow-ups
confirm twelve bootstrap-gate crossings: List, Queens, Binarytrees, Nqueens,
Deriv, GCbench, Levenshtein, Jq BF, Jq Records, Microdiff, Prettier and Log Pipeline.
`p46-validation-unlock.json` records the common executable identity and exact
receipts. Validation then finds the P41 boxed inlining expansion reusing a
recursive caller's guarded parameter lanes, although the new activation enters
through the original ABI. P47 retains that call and its own entry guards
(**D7.4.1 / D4.3.1**). `slug-unit-bisect.json` locates the regression; no package
source or oracle changes. Two fixture defects (reserved `last` parameter and
an emission assertion on a discarded register prefix) were also corrected.

P47 freezes SHA256
`383f171fdab5a6db17c49eb8f5801f4d0a25a453474a5b16b2a8eadca92754fe`.
All 17 focused unit methods pass (`p47-focused-units.log`), including both new
`mir_mvp_open_storage_kernels` and `mir_mvp_erased_primitive_stores` fixture/golden
pairs under both compilers and normal/forced/seeded GC. Its complete untyped
screen passes all 77 goldens: **62 faster / 15 slower Full medians**, geometric
MVP/Full **0.60956** and MVP/P36 **0.89282**. The typed screen finds Raytrace3D
at 11.710 ms versus P36 6.018 ms. Only this task's process group 69558 was
stopped; the typed screen is partial and does not qualify P47. Archived-binary
bisection locates the large regression at P41/P42 boxed inlining
(`typed-raytrace-bisect.json`). Diagnostic profile/MIR collection follows.
P46 timings do not qualify P47, and the every-row goal remains open. No baseline
ran.

P48 retains ordinary array-producing calls unless an existing dense kernel
plan supplies the guards. Their optimized entry versions use
`lambda_fill_primitive_bits`; generic expansion had instead emitted
`array_plain`/`array_push` plus validators. Diagnostic MIR grows from 34,301
lines on P36 to 46,072 on P47, with many more root reloads; those profiled
execution times are not performance evidence. P48 passes five focused unit
methods, including a new caller check on the existing `shifted` fixture.
Frozen SHA256 is
`890fd9ede60635144e4f944834d2624346e27b86fa03a87ec43d633a1c266c13`.
The 15-pair typed Raytrace3D follow-up is 5.976 / P36 5.958 ms, with no
statistical slowdown; its complete typed campaign is running. P47 untyped
measurements remain historical until the corrected candidate is measured.

P48's complete typed screen passes all 77 goldens (five pairs). Its geometric
MVP/P36 ratio is 0.99010 over 76 nonzero rows. Nine flagged rows receive
15-pair follow-ups; matched 100,000-kernel Sumfp batching covers the zero-clock
row. Three flags remain: Deriv and both Richards wrappers. The queued untyped
rerun is correctly withheld. Richards profile counts locate an unintended
change to previously inlined native scalar-result mutators: the new open/map
mutator rule adds 533,550 `tcb_is_held_or_waiting` calls per workload and many
other entries. P49/P50 restrict that rule to the newly enabled boxed-value
family, preserving existing scalar, void and discard paths. Deriv has identical
normalized MIR and runtime counters to P36; an independent timing check is
pending rather than an unsupported source change.


P50 freezes SHA256
`b6ae963fe5db8f4eb76038e0726dc77e49c8d1e642eceb3457f091939dd2628d`.
Six focused unit methods pass. All four targeted typed follow-ups clear:
Deriv's independent 30-pair check is 5.3125 / P36 5.3385 ms; the 15-pair
AWFY/JetStream Richards checks are 85.426 / 85.130 and 86.294 / 86.377 ms;
Raytrace3D is 5.944 / 5.945 ms. None has a statistical slowdown. The P50
complete untyped comparison is running; P48 supplies the last complete typed
screen, with the P50 follow-ups qualifying its corrected inlining paths.
No further aggregate typed campaign or baseline has run.


P50's corrected complete untyped screen passes all 77 benchmark goldens for
all three measured engines. Three paired rounds follow one warmup. It has
**61 faster / 16 slower Full medians**; 58 rows pass the stricter bootstrap
Full gate. The geometric MVP/Full ratio is **0.60937** and MVP/P36 is
**0.89681** (10.3% less execution time). All nine binary/source/corpus/runner/
release identity checks pass in `p50-summary.json`. No source benchmark or
benchmark golden changed in this continuation. P47's 62-row median count is
superseded: GCbench and Brainfuck now sit about 0.1% above Full, while Jq Tree
crosses below. These near-boundary median changes are not statistical proof
of a compiler slowdown.

| Remaining untyped row | P50 MVP ms | Full ms | MVP/Full |
|---|---:|---:|---:|
| jetstream/deltablue | 62.915 | 28.254 | 2.22676 |
| larceny/paraffins | 0.283 | 0.215 | 1.31628 |
| awfy/sieve | 0.033 | 0.026 | 1.26923 |
| beng/fasta | 0.885 | 0.765 | 1.15686 |
| r7rs/mbrot | 0.637 | 0.566 | 1.12544 |
| text/three_way_merge | 2018.810 | 1827.430 | 1.10473 |
| larceny/pnpoly | 1.917 | 1.815 | 1.05620 |
| awfy/richards | 293.855 | 280.118 | 1.04904 |
| awfy/deltablue | 76.462 | 73.277 | 1.04347 |
| jetstream/richards | 297.025 | 285.484 | 1.04043 |
| awfy/towers | 0.522 | 0.502 | 1.03984 |
| kostya/base64 | 134.046 | 128.959 | 1.03945 |
| beng/pidigits | 0.307 | 0.302 | 1.01656 |
| jetstream/splay | 210.361 | 207.508 | 1.01375 |
| larceny/gcbench | 97.630 | 97.490 | 1.00144 |
| kostya/brainfuck | 252.903 | 252.625 | 1.00110 |

The all-row goal remains active. No next unit suite or aggregate comparison
is planned until the next batch clears the requested eleven-case threshold;
focused release timing retains the existing golden sanity checks. Potential
shared roots for the next implementation batch (not implemented or measured):

- `inline_call` retains staged argument registers but discards their admitted
  scalar producer facts. Investigate per-activation facts for unchanged plain
  parameters; retain receiving contracts and guard/ABI separation
  (**D4.3.1 / D7.4.1 / D6.3**). This may help small numeric/decimal helpers.
- `strided_array_loop` never supplies the primitive erased-owner predictions
  already used by `array_iteration`, and requires two accesses. Investigate
  guarded whole-loop versions for single-store primitive strides, amortizing
  live COW checks (**D4.3.3 / S9.1.2**).
- `scalar_place_store` already performs a guarded noncollecting typed spine
  walk. Extend that shared shape using separately guarded inferred layouts;
  preserve tag checks for boxed children, every ancestor's COW permission and
  the generic setter's fallback. Do not turn a construction candidate into a
  receiving contract (**D4.3.1 / D2.6.2 / S9.2.2 / D6.3**).
- Generic ordered map fields still use descriptor-dispatch `set_field_value`
  even for fixed `any`/TypedItem slots. Investigate exposing/reusing the
  existing `typeditem_store_item` kernel through a Lambda-owned audited C ABI;
  do not copy it or edit its sibling owner (**D2.6.2 / D6.3 / D9.1.1v4**).


## 10.97 P51 onward: second deferred-validation performance batch

The latest user instruction requires **more than ten** newly faster cases before
validation. P50's 16 slower rows form this batch's target set; frozen P50 is
the previous-MVP control and P26 still supplies Full. Focused release timing
probes keep golden sanity checks. Unit suites, aggregate campaigns and typed
regression qualification remain deferred until at least 11 crossings. A few
lower medians alone do not establish the gate.

P51 replaces eight byte-wise loads for packed 64-bit values with a byte-address
register and an offset-zero load on the supported unaligned-word hosts. Strict
targets retain the byte reconstruction. This changes no storage or decoding
rule (**D2.6.2**). GCbench measures 82.239/Full 96.644/P50 93.950 ms and has
a bootstrap Full win. Other six-record probes do not cross.

P52 retains nonnullable scalar facts already proved for unchanged actuals when
inlining an open parameter. Opaque inline actuals can select the same guarded
scalar entry body as an ordinary call. Receiving contracts and original ABI
effects remain separate (**D4.3.1 / D7.4.1 / D6.3**). P53 extends the existing
affine stride version to erased primitive owners, including single-access
stores; all three lanes reuse the iteration planner and owner/COW guards.
Sieve improves from P50 0.033 to 0.026 ms, against Full 0.025; Brainfuck
measures 248.236/253.243 ms without a bootstrap Full win. Neither counts.
P54 applies the finite-radius integer proof inside an exact scalar entry
version. It retains the original error/GC ABI and ordinary checked body.
Paraffins improves to 0.259/Full 0.209/P50 0.273 ms without crossing.

A text extraction initially matched a forward declaration and changed earlier
functions. The failed build was never executed. Recovery exactly matches the
frozen P52 source SHA256, after which the stride extraction was reapplied only
to its definition. `p52-recovered-mvp_mir.cpp` and the failed edit/build artifacts
are retained under `temp/mvp_untyped_tuning_goal/`.

P55 extends the existing scalar-place walk to inferred nested paths. Observed
shapes stay separate from receiving contracts. Each ancestor must match its
live layout/descriptor and be writable under ordinary COW; every miss retains
the canonical setter (**S9.1.2 / S9.2.2 / D7.4.1**). Unknown array children keep
their Item tags before a subsequent map guard (**D2.6.2**). Shared field-domain
planning serves both reads and stores; no second path walker was added. P56
adds exact bool/float inputs to that scalar shape, retaining retag fallback.

| P55 focused case | MVP ms | Full ms | P50 ms | Bootstrap Full win |
| --- | ---: | ---: | ---: | --- |
| AWFY DeltaBlue | 43.112 | 73.620 | 76.108 | yes |
| AWFY Richards | 264.368 | 280.487 | 292.129 | yes |
| JetStream Richards | 264.274 | 279.845 | 289.760 | yes |
| JetStream DeltaBlue | 61.800 | 28.464 | 61.043 | no |
| JetStream Splay | 209.772 | 205.419 | 208.357 | no |

All focus receipts retain benchmark golden checks; these are not aggregate
qualification. `mir_mvp_inferred_record_paths.ls/.txt` is added to the existing
paired GC unit, but remains unrun per the validation gate. P57 tested scalar
entry versions in mutators, without useful gains and with a Mbrot slowdown
(0.739/P50 0.661 ms). That change was discarded. Ordinary entry emission
consumes var homes after scalar versions, so adding mutator versions in future
also requires integrating their entry publication protocol. Root executables
are untouched; no baseline, sibling edit or commit ran. The goal is active.


P58–P66 retain decimal producer domains for arithmetic (**S4.4 / S4.5**),
use the existing scalar-domain helper for local seeds, and preserve admitted
success information when the implicit boundary discharges Error
(**S11.4.3 / S7.7.3**). P60's Pidigits probe failed compilation because that
filter erased the decimal proof before an unbox. P61 repairs the root cause;
failed P60 timings are excluded. P66 canonicalizes decimal literal observations
before entry versions so a tag guard never claims a literal value refinement
(**D4.3.1 / D7.4.1**). The original decimal ABI, allocation and errors remain.

Erased finite integer keys can use the existing guarded physical Array reader.
Exact bool equality uses its own value family and retains Error precedence;
erased string equality guards both tags before the shared content comparator
(**S5.1.1v2**). Integer, float and string alternatives share one input-guard
shape. Full retains its existing lowering and ordinary boxed fallbacks.

P62 admits immutable string arrays to the existing literal/closed-call domain
proof. Numeric-array assumptions remain in `mvp_array_element`; pointer slots
use the existing reader. P63 adds a registry relation for a known string
separator: it rejects the numeric section-count split overload, so normal
results contain String parts even for an erased source. Both `split` arities
retain Error; numeric-count calls retain their old relation (**S17.1.1 / S8.2 /
D4.3.1**). P64 preserves an unmodified local's string-element domain across
plain callees' COW snapshots (**S9.1.2**). This closes Three-way Merge's gap.
The affine store loop may also guard the enclosing interval endpoint when it
stays inside the existing arithmetic proof, avoiding an endpoint division.

P65 reuses the shared AST's completed projection live-range/escape proof.
`mir_place_copy_snapshot_required` is shared with Full; unknown ranges retain
a snapshot. MVP skips only the initializer's redundant capture when the
projection dies before the source write (**D5.4.5 / S9.1.2**). The COW diagnostic
for JetStream DeltaBlue changes Array copies from 86,936,320 to 78,309,280
bytes; Full uses 23,958,080. The remaining difference is unresolved, not evidence
that further snapshots may be removed.

P65 independently reconfirms the seven candidate crossings on **one frozen
release**, five pairs plus one warmup, using P26 Full and P50 prior MVP:

| P65 focused crossing | MVP ms | Full ms | P50 ms |
| --- | ---: | ---: | ---: |
| AWFY DeltaBlue | 30.795 | 71.160 | 74.275 |
| AWFY Richards | 217.630 | 278.433 | 286.985 |
| JetStream Richards | 221.249 | 277.742 | 289.779 |
| GCbench | 84.583 | 96.927 | 89.023 |
| Fasta | 0.423 | 0.778 | 0.854 |
| Three-way Merge | 1510.830 | 1787.200 | 2002.700 |
| Base64 | 125.825 | 129.016 | 135.018 |

All seven have bootstrap Full win flags and benchmark golden sanity checks.
`p65-validation-gate.json` records **7/11**, `unlocked: false`. No aggregate
77-case count or typed qualification is inferred from this subset. The subsequent P66 Pidigits follow-up has a decisive win (15 pairs/3 warmups):
0.286/Full 0.299/P50 0.304 ms. Its receipt is
`p66-untyped-long-beng-pidigits.json`; this is an eighth distinct crossing,
not an 11-case gate or an aggregate qualification. Sieve, Paraffins, Pnpoly, Mbrot,
Towers, Brainfuck, Splay and JetStream DeltaBlue remain tuning targets.

New/extended deferred unit cases cover exact/wide scalar-version resumption,
decimal/null/Error outcomes, inferred record-path retags/COW, invalid tagged
children, dead/live/escaped projection observers, non-dividing primitive
strides, string/bool cross-family equality and both split overloads. No fixture
or oracle has been weakened and **none of these new checks has run yet**.
P50 remains the last aggregate-qualified release; P48's complete typed screen
plus P50 changed-path follow-ups remain the latest typed evidence.

Next shared roots:

- `tail_call` rejects Item results and same-owner mutable borrows. Towers still
  enters 5,460 `move_disks` frames and 8,191 `pop_disk_from` frames; the release
  diagnostic MIR/profile is under `p65-diagnostic-awfy-towers.*`. A bounded
  self-tail loop could preserve the same precise frame and original var homes,
  but must prove plain actual domains, return admission and all entry facts.
  No such relaxation has been implemented.
- The existing dense function entry can prove typed array kernels, while open
  var-array parameters remain erased. Explore physical primitive candidates
  inside that existing proof/guard path, retaining its checked sibling and
  once-consumed var homes. Do not add a scalar-style mutator body before those
  homes are consumed. This may help Sieve and remaining array kernels.
- Investigate the remaining DeltaBlue COW-copy difference using source lifetime
  and shared AST facts. Do not remove captures merely to match Full counters.
- Preserve original ABI/effects in finite arithmetic versions. Paraffins still
  misses; its called arithmetic needs a proof that actually reaches the hot
  loop, rather than more speculative body duplication.

The goal remains active. Only focused release timing/profiling was run after
P50; unit suites and all aggregate validation remain behind the 11-win gate.

P66 is frozen by `p66-manifest.json`, SHA256
`2997ec565b4541c3ad226f0110e3c8bf8d2bbd748a4c2f520dffe9998a59865a`.
Its only compiler delta after the P65 repeat is canonical decimal literal
observations. Eight distinct targets have focused Full wins across P65/P66;
the gate still requires 11 on one final candidate. No processes remain
running. Continue from these artifacts; do not rerun the old aggregate
qualification or units before the user's threshold is met.


## 10.98 P67 onward: erased array entries and integer leaves

The user requires **more than ten** former slower rows to cross Full before
validation. This overrides the original goal tool's stale six-win wording.
Focused release probes retain their benchmark output oracles; no unit suite,
typed/aggregate campaign, Lambda baseline or Radiant baseline has run after
P50. No overall 77-case qualification is inferred from these probes.

P67 allows bounded self-tail loops with an open Item result and unchanged
mutable borrows. Every borrowed actual must name the original formal home;
plain actuals must preserve their native admission domains. Task scopes,
captures, methods, binders, projected snapshots and shared homes retain the
real-call path. This preserves original precise owners and Error publication
(**D5.4.6 / D6.3 / S7.6.3v3**).

P68 observes primitive constructor element candidates for erased parameters.
It reuses the existing whole-function dense proof, physical tag/lane/extent
and COW guards; Item ABI and receiving contracts are unchanged. Failed proofs
restore the original bindings. P70 preserves these new opaque array-loop
entries instead of expanding their generic siblings. P69 directly classifies
boxed true/false before the existing general truth helper; other families,
including Error, keep their original behavior (**D4.3.1 / D7.4.1 / S3.1v2**).

P71 extended that inlining policy to pre-existing privately inferred array
contracts. P72's Pnpoly receipt showed a statistically slower result against
P50 (2.084/Full 1.806/P50 1.862 ms). **That extension is discarded in P73**:
only newly opaque guarded entries affect the policy. P73 Pnpoly recovers to
1.847/Full 1.798/P50 1.908 ms; it remains slower than Full.

P72 lets cached physical array readers guard an erased finite integer key
before using the existing lane. Invalid or effectful keys keep `fn_index`.
This preserves total indexed-read semantics (**S7.1.1v3**). P73 focused results,
five pairs and one warmup, reconfirm the two additional crossings:

| P73 focused case | MVP ms | Full ms | P50 ms | Full bootstrap win |
| --- | ---: | ---: | ---: | --- |
| Sieve | 0.012 | 0.025 | 0.033 | yes |
| Towers | 0.421 | 0.484 | 0.507 | yes |
| Pnpoly | 1.847 | 1.798 | 1.908 | no |

Together with P65's seven and P66's Pidigits, these are **ten distinct focused
crossings across frozen releases**, with validation still deferred. The
pending `mir_mvp_open_array_entries` and `mir_mvp_borrowed_tail` fixtures cover
wrong carriers, exact/noninteger/Error keys, shared owners, borrowed error
write-back and changed-owner recursion; **neither fixture has run yet**.

P74 factors the existing integer leaf syntax proof and queries call-site
integer success domains without changing receiving contracts or real-call
errors (**D4.3.1 / D7.4.1 / S7.7.3**). Closed array bodies can reserve pure
integer expansions in their existing reversible effect plan. P74 Paraffins
still misses: 0.260/Full 0.201/P50 0.275 ms. Its release diagnostic records
720 real `ms2` calls from `count_ccp`; no hot indexed-read helper calls remain.
P75 applies the same bounded leaf/effect plan to guarded primitive iterations.
All leaf expansions reserve their aggregate cost before raw owner witnesses
are exposed; the emitted region retains its noncollecting-call audit. P75's
focused Paraffins probe is in progress. Integer/floating/null/Error/infinity
fixture cases are prepared, but deliberately unrun until the gate opens.

Artifacts: `temp/mvp_untyped_tuning_goal/p67` through `p75` manifests,
release executables, focus receipts and P74 Paraffins diagnostic MIR/profile.
The shared root executable remains untouched; no sibling source edit or
commit occurred. The goal remains active.


P76–P79 are diagnostic releases, excluded from timing evidence. Their temporary
compiler logs found that scalar leaf proof alone cannot close Paraffins' loop:
its accumulator is an external Item, and the larger inner body exceeds the old
256-node iteration budget. P80 extends the existing guarded float-outcome path
to initialized, unaliased local integer seeds. Each selected scalar has an
exact physical entry guard; the closed body must prove every write, and the
ordinary Item home receives the result afterward. Shared, captured, borrowed,
module and state homes are excluded (**D4.3.1 / D6.3 / S12.2.1v3**). The bounded
iteration size is now 512, matching the existing whole-function kernel budget;
total leaf expansion reservation stays 256. No source algorithm is changed.

P81 canonicalizes literal scalar seeds using `mvp_inferred_scalar_contract`;
without this, a literal integer's refined Type pointer misses the canonical
integer candidate and the accumulator stays boxed. This is a producer-domain
fact, never a literal receiving constraint (**D4.3.1 / D7.4.1**). Temporary
logs are removed. The five-pair Paraffins probe has medians 0.198/Full 0.211/
P50 0.282 ms but no bootstrap win. The **31-pair/3-warmup** repeat confirms
0.192/Full 0.210/P50 0.286 ms, with bootstrap wins against both controls
(`p81-untyped-long-larceny-paraffins.json`). This is the eleventh distinct
focused crossing across versions. The other ten are being reconfirmed on
frozen P81 before any unit, typed or aggregate validation. No validation gate
is yet claimed from mixed-candidate receipts.


The final P81 gate follow-ups confirm Fasta 0.424/Full 0.803/P50 0.873 ms and
Pidigits 0.294/Full 0.310/P50 0.313 ms (31 pairs, three warmups). The other
eight P81 five-pair gate receipts and the longer Paraffins receipt all have
bootstrap Full wins. `p81-validation-gate.json` verifies the eleven receipts
identify the same candidate SHA and records `unlocked: true`. The earlier
inconclusive samples remain on disk. Eleven focused unit methods, including
normal/forced/seeded-GC fixtures and MIR assertions, are now running; no unit
or aggregate result has yet been claimed. The broader goal remains active.


P81 deferred units: nine of the original eleven methods pass; two new
fixtures initially fail both frontends because of reserved names (`state`,
`number`) and explicit Error-admitting owner parameters on discarded nested
writes (**S11.4.3 / S7.1.3v3**). The fixture names and implicit untyped
parameters are corrected without changing either golden. After correction,
MVP passes all normal/forced/seeded-GC cases. Paired checks reveal two existing
Full defects, independently reproduced on frozen P26: invalid projected
string writes and different-owner recursive borrow publication. They are
recorded as **LR07-54 / LR07-55** in the Lambda ledger, with original failure
receipts retained. Existing paired fixtures remain paired; the two additional
MVP fixtures have explicit MVP-only unit methods, as for LR07-53, and retain
their unchanged semantic goldens. No Full correctness pass is claimed for
those cases. The four corrected methods pass (`p81-corrected-focused-units.log`),
alongside the nine passing methods in `p81-focused-units.log`.

The first P81 aggregate untyped screen is running: all 77 registered cases,
five pairs and one warmup against P26 Full and P50 prior MVP. The binary and
benchmark corpus remain frozen. No benchmark source, benchmark golden or
compiler source changed after the eleven-win gate.


P81 aggregate untyped result (`p81-untyped-all.json`): **77/77** golden
outputs match in all three engines; no engine failures. Five pairs and one
warmup yield **72/77** lower MVP medians against frozen P26 Full, **68**
bootstrap Full wins, and geometric MVP/Full ratio **0.5331865**. P50's last
aggregate screen had 61/77 median wins. The five remaining slower medians are:

| Untyped case | P81 ms | Full ms | P50 ms |
| --- | ---: | ---: | ---: |
| Nqueens | 0.753 | 0.746 | 0.762 |
| Pnpoly | 1.878 | 1.798 | 1.873 |
| Mbrot | 0.654 | 0.574 | 0.676 |
| Splay | 213.108 | 209.256 | 212.929 |
| JetStream DeltaBlue | 56.377 | 28.667 | 63.021 |

Three small prior-MVP regression flags need follow-up: untyped Matmul
4.021/P50 3.947 ms, AWFY CD 310.022/302.604 ms, and Julia Parse Integers
50.531/49.386 ms. They still beat Full; do not erase these flags from the
aggregate receipt or infer regression-free qualification. Brainfuck crosses
Full in this screen as well (248.768/256.028/P50 251.962 ms). JQ Tree is
1488.620/Full 1488.700 ms, an unresolved near tie rather than a robust win.
The geometric ratio summarizes the measured corpus; it is not a claim that
every case meets the goal. Timings exclude compilation.

The P81 complete typed regression screen is running against P50, five pairs
and one warmup. No typed performance qualification is yet claimed. The goal
remains active with the five slower rows, the near tie and regression flags
recorded for further tuning. No baseline or commit ran.


P81 complete typed screen (`p81-typed-all.json`): **77/77** candidate/prior
MVP goldens match, no engine failures. Geometric MVP/P50 is **0.9930285**
over 76 nonzero timer rows. Typed Sumfp measures 0.000 ms in both engines and
requires the existing batched supplement. Two regression flags survive the
31-pair/3-warmup confirmation: FFT 0.037/P50 0.034 ms and Brainfuck
204.810/199.751 ms. **Typed regression-free qualification is not claimed.**
The full aggregate and both confirmation receipts remain unchanged.

P82 fixes the two demonstrated roots. Normalized MIR for FFT differs only in
three removed endpoint instructions: the enclosing counter endpoint combined
with a shifted key exceeds the owner even though the actual last visited key
fits. The whole-loop guard then always misses. Keep the exact last counter
for affine offset keys, retaining the cheap enclosing endpoint for direct
counters and invariant keys (**S7.1.1v3 / D4.3.3**). Brainfuck's admitted String
owner cannot pass a physical Array guard. The expanded boxed-key reader must
not emit this impossible alternative; it caused extra checks and root reloads
before the unchanged canonical string reader on every iteration. Keep the
boxed-key Array fast path for open/union/array owner families, preserving all
other canonical indexed reads (**D4.3.1 / S7.1.1v3 / D6.3**).

P82's focused witness method passes normal/forced/seeded-GC goldens under
both compilers plus MIR assertions. New cases cover shifted strided stores
with unique/shared owners and a known string with erased keys; exact endpoint
arithmetic remains in the former and dead Array-descriptor checks are absent
from the latter. The first MIR assertion was too broad and rejected legitimate
snapshot flag stores; it is corrected to identify Array view descriptor loads
without removing snapshot coverage. Its failed receipt remains archived.
P82 longer typed FFT/Brainfuck repeats are running. Compiler sources match
`p82-manifest.json`; no aggregate P82 qualification is inferred from P81.


P82 resolves both confirmed typed flags in **31 pairs / three warmups**:
FFT 0.034/P50 0.034 ms, Brainfuck 196.461/P50 196.861 ms; neither has a
statistical regression flag. Receipts: `p82-typed-confirm-r7rs-fft.json` and
`p82-typed-confirm-kostya-brainfuck.json`. The narrowed String probe and exact
shifted endpoint are the only compiler deltas after P81's aggregate screens.
The matched typed Sumfp batch (100 repetitions) is running to resolve the
zero-duration row; focused untyped affected-path/flag follow-ups remain.
No complete P82 typed/untyped campaign has run, and the five Full misses from
P81 are still open until further tuning and measurement.


The typed Sumfp matched batch of 100 remains below timer resolution in both
releases: both medians are 0.000 ms, with occasional 0.001 ms observations
(`p82-typed-sumfp-batch100.json`). Its output matches under both engines.
Keep this row **timer-resolution-limited**; do not invent a speed ratio or
add artificial work to obtain one. All 77 typed golden outputs passed P81's
screen, its two measurable regression flags are resolved by P82 focused
repeats, and the unresolved Sumfp precision limit is explicit. P82's three
untyped prior-MVP flag follow-ups are now running, 31 pairs/three warmups.


P82's 31-pair untyped flag follow-ups clear Matmul (3.974/P50 3.962 ms,
no statistical slowdown), while CD (300.299/P50 298.606 ms) and Parse Integers
(49.950/P50 48.576 ms) retain small regression flags. Both still beat Full
(474.624 and 52.818 ms respectively). These flags remain open; do not claim
that all untyped rows are regression-free. Focused post-fix measurements of
the affected array/string paths and the five Full misses are now running.
No complete P82 aggregate has been rerun.


P82 focused post-fix checks retain Full wins for Fasta (0.400/0.678 ms),
Base64 (122.081/127.667), Sieve (0.012/0.025), Towers (0.405/0.492),
untyped FFT (0.198/0.222) and Brainfuck (240.101/258.490), with unchanged
benchmark goldens. Paraffins has lower medians (0.184/Full 0.206) but its
five-pair bootstrap comparison is inconclusive. The five previous misses
measure Nqueens 0.716/Full 0.755, Pnpoly 1.833/1.879, Mbrot 0.637/0.574,
Splay 207.089/203.647, and JetStream DeltaBlue 54.337/27.682 ms. The first
two now have lower medians but no bootstrap win; do not silently replace the
77-case P81 result or claim those misses resolved. Longer Nqueens/Pnpoly/
Paraffins repeats are running. All focused artifacts use frozen P82.


P82 longer follow-ups confirm Nqueens below Full (0.740/Full 0.758/P50
0.742 ms) and reconfirm Paraffins (0.189/0.205/0.282), both with bootstrap
Full wins. Pnpoly remains slower (1.852/Full 1.814/P50 1.908), despite the
lower-median five-pair probe; keep both receipts. Four focused Full misses
remain: Pnpoly, Mbrot, Splay and JetStream DeltaBlue. `p82-summary.json`
records the complete P81 campaigns separately from P82 focused evidence;
`batch2-checkpoint.json` records the current work. P82 SHA256 is
`3b58c779f0fdf7eae93e11f1704ef9ef946088057f30431f9364516a9df6dd7e`.

Fresh release diagnostic MIR/profiles for those four and CD/Parse Integers
are under `p82-diagnostic-untyped-*` and `p26-full-diagnostic-untyped-*`.
Mbrot's `count` already takes five native doubles in **both** compilers; a
lack of scalar ABI inference across the matrix var borrow is ruled out.
Do not retry the discarded P57 mutator version on that assumption. Pnpoly
enters 100,000 real activations in both compilers. MVP's checked sibling
leaves four root slots and uses the shared audited stack helper; Full's
native variant carries `_array_witness` and uses BSTART. Do not simply
switch root-bearing functions to BSTART: shared `em_insert_native_stack_guard`
explicitly records the earlier root-bearing hot-body regression (JC23).
Investigate precise typed-array fallback/effect/owner proofs instead.

P83 is an **unqualified Boolean branch experiment**, with P82 frozen as its
prior-MVP control. `boolean_branch_body` uses MIR BT/BF directly on the
already-computed truth register, retaining the prior Error edge (**S3.1v2 /
S7.9.3**). Mbrot's native float predicate uses `DGT; BEQ ..., 0`, whereas
Full uses `DGT; BF`; this is a general frontend lowering difference to test,
not yet a demonstrated performance cause. Four focused semantic/MIR methods
pass on P83. One older emission assertion also fails P82: it forbids Boolean
Item boxing anywhere before the first fn_index, including valid cold Array
member alternatives added earlier. The corrected assertion requires the
floating predicate register to feed its branch directly, retaining the
intended optimization check and all semantic/GC goldens. Original failures
are kept. P83 versus P82 focused timing on all four Full misses plus the two
small prior regressions is running. Keep the experiment only on evidence.


P83's six five-pair comparisons establish **no bootstrap improvement** over
P82; none of the four Full misses crosses Full. The Boolean branch delta is
discarded. Source identity is again exactly P82's archived compiler identity;
the P82 executable hash also matches. Keep the P83 receipts as a rejected
experiment, not as a speed qualification. Four final predicate/error methods
pass on P82 with the corrected emission assertion
(`p82-final-focused-units.log`). No source or golden algorithm is weakened.

Current checkpoint (local 2026-11-10): **P82 retained**, SHA256
`3b58c779f0fdf7eae93e11f1704ef9ef946088057f30431f9364516a9df6dd7e`.
No build, test or profiling jobs remain running. The 11-crossing gate opened
on P81 before any validation. P81 remains the last complete 77-case screen:
72 faster Full medians / 68 bootstrap wins, all untyped goldens passing;
all typed goldens pass too. P82 focused repeats resolve typed FFT/Brainfuck
regressions; Sumfp stays below timer resolution in both binaries. The latest
focused Full misses are **Pnpoly, Mbrot, Splay and JetStream DeltaBlue**;
Nqueens and Paraffins have 31-pair Full wins. Small prior-MVP untyped CD and
Parse Integers regressions remain. Do not manufacture a combined P82 77-case
count. Structured receipts/state are in `p82-summary.json` and
`batch2-checkpoint.json` under `temp/mvp_untyped_tuning_goal/`.

Continue on the remaining shared roots without repeating aggregate validation
merely to rediscover these rows. The fresh six-case MVP/Full diagnostic
profiles and MIR are ready for analysis. Compare precise typed-array slow
reader effects/frame requirements for Pnpoly, arithmetic/loop lowering in
Mbrot (both already have native double inputs), and Splay/DeltaBlue object
construction plus COW lifetimes. The shared BSTART heuristic has a documented
root-bearing regression; never replace its probe without a sound owner/frame
proof and evidence. P65's remaining DeltaBlue Array-copy difference is still
unresolved; do not drop snapshots to match Full counters. Reuse existing
helpers and retain P50's typed inlining policies. Build the runtime target
before linking the next private release, because the rejected P83 was the
last compiled archive. No root executable replacement, sibling edit,
baseline suite or commit ran. The goal is active and incomplete.


Wrap-up, 2026-11-10: work stopped at the user's request. The interrupted
continuation performed read-only inspection and made no further compiler
change. Process inspection confirms no tuning build, test or profiling job
remains. Retained compiler sources and binary still match P82's manifest.
No additional tests, aggregate campaigns or baselines ran during wrap-up.
Changes remain uncommitted. The goal tool returns no active goal at wrap-up;
the performance objective is **incomplete**, with the four Full misses and
two prior-MVP flags above retained in the checkpoint. Resume from P82 and
the existing profiles; do not use the rejected P83 archive as the retained
candidate.


### 10.99 Resumed tuning: nested primitive stores, reads and constructors (P84–P102)

Resumed on clean `8aca6eeb7` at the user's request, with a new active goal.
P82 sources match its archived manifest; the earlier tuning was committed as
`cc2c40dbe`. P26 remains the frozen Full control, and P82 is the immediate
MVP control. The P81 eleven-crossing validation gate remains satisfied. No
root executable replacement, Lambda/Radiant baseline or sibling edit is used.

P84's proposed coarse Map domain adds no measurable gain on the six focused
rows and is discarded. Its receipts remain. P85 extends the existing guarded
scalar-place walk to an indexed primitive leaf: guard each ancestor's physical
kind, live descriptor and COW ownership, then the exact numeric lane and
bounds. A nonempty representation certificate keeps the checked setter.
Only the final store mutates data; a miss retains the canonical collecting
writer and once-evaluated keys (**D4.3.1 / D4.3.3 / D6.3 / S9.1.2**).
P86 keeps the existing packed N-D arm before this alternative. The inherited
List certificate offset is shared through `em_array_rep_cert`; no runtime
container or type layout changes.

P87 extends the existing singleton-join helper to same-lane numeric prefixes,
with exact source/member/result roots across allocation and existing byte-copy
and admitted-store kernels. Erased singleton joins can select this helper;
alternate scalar/list/N-D/Error domains retain its canonical fallback
(**S10.6.1 / D6.3**). P88 replaces the cold boxed read only for proved rank-one
primitive arrays and native integer keys. `lambda_array_primitive_index`
returns scalar lane bits, handles bounds, empty arrays and live strided storage,
and never allocates or reenters. The registry audits this effect; the planner
and emitter agree. Open/Error/coordinate reads still retain their original
dispatch. Removing cold collecting reads lets Pnpoly retain a rootless entry
without altering the shared native-stack guard heuristic (**S7.1.1v3 / D6.3**).

P88 focused five-pair medians, candidate/Full/P82: Mbrot
**0.363/0.567/0.653 ms**, Pnpoly **1.605/1.857/1.858 ms**, both bootstrap
wins. Splay **207.900/204.120/209.401 ms** and JetStream DeltaBlue
**53.770/28.110/54.157 ms** remain open. These are self-reported execution
boundaries, excluding compilation, with unchanged benchmark goldens. No
complete 77-case P88 campaign is implied.

P89/P90 investigate constructor helper overhead: an ordered mixed record can
store a proved pointer/null field directly even when other fields need the
canonical descriptor setter. Inferred descriptors guard their live type and
flags first; the canonical NO_GC setter remains on a miss. Scalar ownership,
field admission and precise roots stay unchanged (**D2.6.2 / D6.3**).
P89 is an unmeasured intermediate; P90 factors the common fallback and is the
current frozen candidate, pending focused correctness/performance evidence.

The new nested-store fixture covers unique/shared rows, mixed numeric kinds,
retyping, effectful keys and rejected paths. Its first attempt used postfix
`^` on an explicitly infallible helper and is corrected to call it directly;
the semantic golden is unchanged. MVP passes all three GC modes. Frozen P26
Full and P86 Full lose Errors on rejected nested stores, extending the
existing LR07-54 observation. Receipts remain in
`temp/mvp_untyped_tuning_goal/p86-focused-units*.log` and
`p86-nested-full-control.log`; Full correctness is not claimed. The P88
eight-method check passes semantic goldens; two old emission assertions
expect the replaced `fn_index` name. Their replacements now require the
noncollecting primitive helper in the checked sibling. P90 adds permanent
primitive-reader/singleton-join fixture/golden pairs and focused MIR checks.
Typed performance qualification and all-row closure remain pending.


P90's five-pair screen retains Mbrot/Pnpoly Full wins. Constructor mixed-field
stores do not establish a Splay win. The P90 new-fixture attempt correctly
rejects literal `0 div 0` at compile time; the test now produces an int poison
through `int(math.sqrt(-1.0))`. Standalone printed Error messages are raw
text, so the newly authored singleton golden is corrected on both compilers.
The two new reader/join fixtures then pass paired normal/forced/seeded GC
checks, including rootless primitive-reader MIR. The five other P90 methods
pass, including native reduction/branch/copy emission and inferred records.

P91 identifies the main DeltaBlue COW root: `cow_param_retained` includes
retained descendants, while `cow_param_root_retained` distinguishes the
whole root. MVP already captures a retained projection at its callee boundary
(**S9.1.2 / S9.3.1 / D5.4.5**). Pure actuals, a proved receiving boundary and
no parameter writes permit borrowing the whole owner without redundantly
sharing it for that child. Simultaneous var aliases retain ordinary captures.
P92 restricts the newly allowed descendant-retention case to non-reentrant
callees; the existing strict nonretention rule remains. No snapshot/capture
is removed from the actual child-retention boundary.

P91 five-pair JetStream DeltaBlue: **9.330/Full 27.940/P90 53.142 ms**, a
bootstrap win over both controls. Splay has a lower median
214.465/Full 218.110/P90 219.738, but is inconclusive; no closure is claimed.
CD remains a Full win. P91's six focused ownership methods have no MVP
failures; paired Full checks retain LR07-53 and the earlier live-borrow
publication failure. Their goldens remain unchanged. P92 adds a paired
projection-call snapshot fixture for returned/wrapped/captured children,
whole-root returns, local writes, simultaneous borrows and effectful later
arguments. Qualification is pending the frozen P92 release.


P92's six focused methods pass (`p92-focused-units-3.log`), with paired
reader/join/N-D controls and MVP-only oracles where frozen Full retains a
defect. The new projection-call fixture corrects its declared procedure's
body syntax; the required results are unchanged. Full's later-argument
snapshot failure reproduces on P26 and is recorded as LR07-56.

P92 **31-pair / three-warmup** confirmations retain three Full wins:
DeltaBlue **9.639/Full 28.690/P82 55.466 ms**, Pnpoly
**1.733/1.934/1.934 ms**, Mbrot **0.376/0.600/0.684 ms**. Splay remains
slower at **211.924/208.649/211.392 ms**. These are focused receipts, not
a synthetic complete screen. P92 is frozen by its manifest.

P93 investigates the remaining Splay constructor cost. A sufficiently large
primitive literal whose members are all literal nodes can copy an
Input-owned byte template with the audited NO_GC `memcpy`. It still allocates
fresh runtime header/data on every evaluation, retains the certificate
constructor where required, and emits ordinary expression evaluation for
nonliteral/effectful members (**D5.1.2 / D6.3 / S9.1.2**). The reader fixture
now checks independently mutable literal results, float/subnormal bits and
the fresh allocation plus bulk-copy MIR. Both focused methods pass on P93
(`p93-focused-units.log`); its Splay comparison is pending.


P93 has no bootstrap Splay improvement (211.134/Full 205.320/P92
210.529 ms) and is discarded, including its test-only template assertions.
Retained compiler sources again match P92. Its typed 77-case, five-pair
regression screen is running against P82; all completed outputs match. The
runtime archive was last compiled as P93, so rebuild it before a later link.
`batch3-checkpoint.json` records the active goal and next investigation.


P92's complete typed campaign passes **77/77 outputs**, five pairs/one warmup
against P82. Over its 76 nonzero rows, geometric MVP/P82 is **0.99897**;
Sumfp remains timer-resolution-limited. It flags Bounce (0.027/0.026 ms),
Splay (520.056/510.941) and Fast Diff (90.091/87.925). These flags remain
open until longer repeats; no regression-free claim is made.

P94 fixes a demonstrated producer-fact gap: the emitter already converts a
proved u32 to an int/float scalar, but `mvp_value_contract` returned `any`
for that input. Reuse the registry's successful result type for this exact
numeric family and preserve an incoming Error independently. No receiving
contract changes (**D4.3.1 / S7.7.3 / S4.4.1**). Diagnostic MIR confirms
Splay's `next_random` result now uses a double lane and `generate_payload`
takes its tag as a double; both were boxed in P92. The new sized-conversion
fixture passes both compilers across all GC modes, preserving Error payloads.
Its first ABI assertion incorrectly demanded a native tag despite an explicit
opaque Error actual; it now checks the actual random-value producer's native
float result. The opaque Error case remains unchanged.

P95 discards P89/P90's mixed-field constructor experiment: it established no
Splay gain and adds inferred descriptor guards to the typed Splay MIR. The
original constructor policy is restored. Bounce/Fast Diff diagnostics show
fewer calls/instructions after the primitive-read change; their longer
measurement is still needed, rather than changing inlining on speculation.
P95 is the current frozen candidate. Ten focused unit methods pass
(`p95-focused-units.log`), including all five new fixture/golden pairs and
normal/forced/seeded GC. Its untyped Splay 31-pair confirmation is running.
No complete P95 aggregate or typed regression clearance is implied.


P95's 31-pair repeats confirm all three typed flags. P96 limits the native
read fallback to bodies that can lose a collecting frame; Bounce and Fast
Diff recover in 15 pairs. Parameter admission alone must not select this
cost policy, so P99 records `body_may_gc` separately while leaving the public
effect contract unchanged. Pure primitive readers retain the NO_GC sibling;
already-collecting bodies keep their previous boxed cold path (**D6.3**).
Normalized P96/P82 timed typed-Splay functions are identical, but its
0.8% timing flag remains open.

P97 uses the proved expression domain when deciding whether a literal field
needs a list image, instead of the broader original AST type. P98 retains
an exact float lane across allocation into an `any` field and uses a small
NO_GC destination-owned TypedItem setter; alternate live descriptors delegate
to `set_field_value`. Packed storage uses memcpy, preserving negative zero,
subnormal and NaN bytes (**D2.6.2 / D6.3 / S11.1.6v2**). Five semantic/MIR
methods pass; a sixth exposes the overly broad P96 parameter-effect policy,
fixed in P99. P98 Splay is 207.118/Full 205.341/P82 210.722 ms (five pairs),
still not a Full win.

P99 permits trusted declared records' fixed-width pointer fields in the
existing all-native constructor. Requiring the descriptor `native` flag
excluded some declared pointer contracts even though `field_has_direct_lane` proves
their storage width. Inferred pointer layouts remain on the checked setter;
field admission, insertion captures and precise roots remain unchanged
(**D4.2.5 / S9.1.2 / D6.3**). Eight focused methods pass, including paired
normal/forced/seeded GC for a mixed array/float/nullable-child record, the
sized conversion/packed float cases and rootless primitive-reader MIR.
P99 focused typed and untyped performance qualification is in progress.


P99 focused typed repeats have no slowdown flags: Splay 513.556/P82
511.294 ms, Bounce 0.026/0.026, Fast Diff 89.130/89.234 (15 pairs).
Untyped Splay remains slower, 210.233/Full 207.629/P82 212.552 (five pairs).
The trusted-shape native constructor is exercised by the new mixed-record
case. A diagnostic P100 removal confirms that generic descriptors for some
declared pointer contracts still require this clause; P101 restores it.
Ordinary pointer bases already set `native`; that flag is not uniformly
scalar-only. P97's proved-domain imaging also reduces the typed constructor.

P100 removes list imaging for array-literal producers, which always produce
an array or Error, and hoists the payload address across contiguous primitive
literal stores. Only literal primary nodes qualify; effectful members retain
the existing per-expression owner reload (**S11.1.6v2 / D6.3**). It keeps fresh
allocation per evaluation and does not share a constant array. The reader
fixture checks independently mutable int/float/bool literal results and
requires one payload load per literal, without slot-image calls.


P100's new fixture initially leaves a fallible projected store unacknowledged
and fails E228 on both compilers. P101 routes it through an explicit `any`
procedure and propagates its outcome; expected snapshots are unchanged.
P100 also deliberately removes the P99 trusted-pointer clause to check
necessity; its native-constructor MIR test fails, so P101 restores it with
the corrected descriptor rationale above. No failure is waived.


P101 passes ten focused methods (`p101-focused-units.log`). Its 31-pair
Splay result is 208.439/Full 207.540/P82 212.163 ms: a bootstrap improvement
over P82, but still 0.43% slower than Full. No all-row target is closed.

P102 revisits a narrower part of the discarded P84 kind proof after repairing
its consumers. Private functions whose every normal return is a record can
keep `TYPE_MAP` across differing layouts; a later non-map return widens to
the original contract. Receiving contracts and Error transport remain intact
(**D4.3.1 / D7.4.1 / S7.7.3**). The emitter now uses that semantic kind to
avoid list imaging, and generic-map native results retain the existing
record inlining policy instead of losing expansion. No Map parameter
narrowing or field-layout proof is added. Permanent paired/GC coverage adds
recursive differently shaped records and a mixed int/map widening case.


**Wrap-up, 2026-11-10:** P102 passes five focused methods, but its five-pair
Splay 211.071/Full 208.208/P82 212.352 ms establishes no gain. DeltaBlue
9.397/28.328/55.190 and CD 303.753/484.235/309.931 retain their wins.
P102 is discarded; compiler sources again match P101's recorded experiment
source manifest (`p101-wrapup-source-check.json`). The runtime archive still
contains P102 objects and must be rebuilt before another link. P101's ten
focused methods passed before the experiment; its final paired/GC constructor
method also passes with the additional recursive and mixed-kind cases
(`p101-wrapup-units.log`). Only P102-specific carrier assertions are removed
with the discarded optimization; semantic goldens remain.

No jobs remain running. Work is paused at the user's wrap-up request; no active goal remains.
The every-untyped-row Full target and current complete typed no-regression
qualification remain open. No complete P101/P102 aggregate, Lambda/Radiant
baseline, shared executable replacement, sibling edit or commit is claimed.


### 10.100 MVP is the default Lambda MIR compiler

The user requested the compiler switch on 2026-11-10. Unset or empty
`LAMBDA_MIR_COMPILER` selects MVP. Explicit `production` retains Full MIR for
comparison. Compiler selection is independent of `interp`/`auto`/`jit`; AUTO
starts in T0 and promotes through the selected compiler. Strict `interp`
never compiles code. This implements the existing tier policy, with no new
semantic ruling (**D8.3.1–D8.3.4 / D8.5.1**). No-MIR builds keep their
interpreter default and reject an explicit unavailable MVP request.

Interpreter satellite compilation previously bypassed the eager selector. It
now dispatches to MVP or Full before lowering. Both use the existing snapshot
pools, code-generation lock, linker, cancellation, Script-owned image lifetime
and evaluator safe-point publication. Common module names/artifacts move to
`mir_module.hpp`; no Full lowering is called from the MVP adapter. The MVP
index uses the existing complete Lambda traversal so copied for clauses and
other language-specific edges remain indexed (**D8.2.4 / D8.3.4**).

Private continuation parameter/iterator sites retain their original binding
identities through `MvpBinding::declaration`. The retained AST and T0 slots
are not retagged. Public satellite entries use checked boxed parameters and
the MVP public return protocol. External callees, imports and globals resolve
through the resident T0 slab. Statically known out-of-image calls reuse MVP's
argument admission and precise borrow/write-back planning, then invoke the
existing `fn_call_borrowed_into` dispatcher. Variadic actuals are unpacked from
the already evaluated rest array, without evaluating source arguments again
(**D7.4.1 / S9.1.3v3 / D6.3**).

Property keys read each image's suffix base, assigned at publication. MVP's
compiler-only constants remain in the image's owned snapshot list and BSS;
they do not extend the running T0 constant table. Type-registry growth still
fails closed. Cached imports retain their logical owner/slot identities
(**D8.3.5 / D8.5.1 / D6.3**).

Focused handoff checks exposed a T0 planner undercount: projected `var`
preparation runs while the callee and its frame-owned argument span remain
rooted. `plan_need` now includes that span rather than only the old callee
root. The for-loop continuation also starts at its retained walk position
instead of replaying the prefix (**D6.3 / D8.3.2**). Existing semantic goldens
are unchanged. The recent merge's stale output-demand forward declarations
in MVP are aligned with their existing definitions so current HEAD builds.

Validation receipts are under `temp/mvp_default_switch/`. The private release
build succeeds. Twelve focused methods pass in `focused-units-9.log`,
including selector/tier dispatch, actual native promotion, property-key/var
transport/float GC cases, handoff, recursion, constructors, N-D values and
snapshots. Eighteen external-call GC subcases pass in
`external-call-units.log`. Separate whole-script, asynchronous publication
and strict-T0 projected-borrow probes match unchanged goldens with no runtime
errors (`lifecycle-probes.json`). Final qualification passes **13 focused Python methods** against the rebuilt
root `lambda.exe` (`final-focused-units.log`) and **9 selected Lambda GTests**
(`focused-gtests-2.log`). The first GTest attempt used the old root executable
because the split makefile defaults to `lambda-static.exe`; explicitly linking
`TARGET=../../lambda.exe` resolves the selector launch failure. Both root
executables now include the switch. `final-summary.json` records release
configuration, binary hashes, changed runtime source hashes and HEAD
`751e08b3b` plus this uncommitted integration. No jobs remain running.
No Lambda/Radiant baseline, benchmark sweep, sibling edit or commit is used.

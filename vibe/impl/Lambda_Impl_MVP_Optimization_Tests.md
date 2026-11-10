# MVP MIR optimization test migration

**Date:** 2026-11-10. **Starting HEAD:** `cf0caf5c7`, main checkout, `master`.

## Current checkpoint

- Follow-up at `c2f1d2426`: 148 of the 197 remaining legacy fixture
  failures are resolved. The final controller has 235/284 passing emission
  contracts and 284/284 passing semantic goldens in all three GC modes. See the
  follow-up section below for root fixes, coverage and current qualification.
- User requested migration of obsolete MIR optimization tests and new MVP
  emission coverage. The preceding audit is under
  `temp/mvp_optimization_coverage/coverage-report.md` (LR07-60).
- Preserve intended native/checked behavior and exact cost review; do not
  suppress implementation failures or restore retired Full helpers (D8.6.1,
  D6.3, S9.1.2–S9.1.3).
- Existing unrelated changes moved runtime declarations into Lambda ownership.
  Their initial diff/hashes are saved under `temp/mvp_emission_test_update/`.
  Concurrent commit `928281c28` captured that split and part of this migration.
  Preserve it; use current source hashes for qualification. Sibling/vendor
  sources remain read-only. Fixed Lambda-owned include dependencies exposed by
  the split in the reactive-render adapter and RootFrame header.
- Scope: explicit MVP function/callee selectors; reviewed helper/path
  assertions and frame budgets; robust telemetry controls; focused Python gate
  registration; small typed/untyped fixtures covering native paths and misses.
- The follow-up fixes LR07-61 on release and LR07-63. LR07-62 and LR07-64
  remain failing controls; the debug face of LR07-61 still needs qualification.
  Missing tunings are not migration excuses.
- Build private controllers/engine as needed, preserve shared `lambda.exe` and
  run unit gates only. No Lambda/Radiant baseline or timing campaign.
- Implemented 548 explicit MVP selectors and 93 callee/wrapper updates. Added
  `{{n}}` ID and `{{p}}` prototype tokens with exact callee/argument controls;
  626 helper assertions now ignore ABI suffix spelling. Shared harnesses accept
  a private `LAMBDA_TEST_EXE` without replacing the shared engine.
- Registered the focused Python emission gate and extracted the third Python
  script adapter's common shape. Five report/selection contract units pass.
- Added five typed/untyped kernel fixtures, manual goldens and 27 structural
  groups, plus platform-aware outcome-lane coverage. All new JIT goldens pass in all three GC modes;
  the new argument-order case exposes a T0 snapshot discrepancy (retain golden).
- Reconnected planned-effect fault injection to MVP's actual plan/verifier.
  The negative claim now rejects through MVP's emitted-effect verifier; empty
  and unmatched-name controls execute successfully (D6.3.2). The release build
  and private controllers are qualified below. Further old-corpus contract
  review and compiler optimization work remain; the complete gates are not green.

## Acceptance and verification budget

Every migrated contract must select an actual MVP body, retain a positive
witness of the optimization and preserve an appropriate rejected/checked
control. Missing instrumentation must fail rather than count as zero. The
normal Lambda gate must run the focused MVP unit selection. New `.ls` files
carry semantic goldens and MIR sidecars. Record all remaining implementation
failures and tested source/binary identities; do not call a partial gate green.

The emission ratchet retains reviewed, zero-slack budgets (D8.6.1). Current
MVP emission may differ structurally from Full; budget changes must describe
the new ownership/guard/frame shape rather than mechanically raising limits.

Focused validation covers matcher contracts, Lambda emission and optimization
units, the new gate/report adapter, Lambda frame budgets and selected JS matcher
regressions. Broader suites run only if an implementation change requires them.

## Initial migration contracts and qualification

The shared matcher keeps explicit MVP function namespaces and exact helper
callees; ABI prototype suffixes are not optimization facts. It now has three
positive/negative matcher units. All 32 formerly required retired-helper
mentions use current kernels or explicit native-path requirements. The latter
remain red where MVP lacks the tuning, including nullable packed construction
and inferred append builders. No retired helper is restored to satisfy a test.

Execution-profile checks use emitted MVP bodies and real executed calls in
place of Full-only field/dense/layout events. The multi-source record test
requires the two bodies and their generic member reads before excluding shape
speculation. A missing function counter returns a failure sentinel, never zero;
its negative unit pins that behavior. Contextual record construction tests now
require destination-shaped allocation with dynamic field validation, and accept
zero graph reifications. Copy ceilings allow an avoided copy while preserving
semantic goldens. The string/TCO/admission-work limits are not inflated.

The five new fixtures cover typed/erased numeric loops and divmod, guarded array
loads/stores and misses, COW snapshots, direct/boxed/closure calls, wide outcome
transport, frozen string observations, Unicode/absence, packed record fields,
and invalid dynamic union fields (D4.3.3, D6.2.1, D7.4.2, S9.1.2–S9.1.3v3).
The POSIX companion-result ABI is exercised on this host; Windows has a separate
assertion branch but was not executed here.

Private release host SHA256:
`15b99367361d4a2f7482c7ded6f2966cf4958721348afc4a5f70c5f55efd1ac1`.
Shared `lambda.exe` remains unchanged at
`c6f6766f9e28aacec482d5762b90a4b22be885c21f2ee1eddf37b053a385a3aa`.
Receipts, raw reports, budget observations and controller/source identities:
`temp/mvp_emission_test_update/`.

| Focused gate | Result | Receipt |
|---|---:|---|
| New fixtures + matcher + outcome ABI + profiling root parity | 10/10 | `qualified-new-emission.json` |
| Lambda emission/frame ratchets + config | 18/18 (17 probes) | `final-ratchet.json` |
| Optimization contracts (mixed JIT/T0/JS) | 46/56 | `final-qualified-opt.json` |
| Registered Python MVP emission/ownership gate | 54/57 | `registered-gate/test_summary.json` |
| Gate adapter reporting/selection units | 5/5 | `gate-adapter-final.log` |
| Shared-matcher JS regression selection | 7/7 | `js-matcher-regressions.json` |
| Node aggregate-runner contract file | pass | `runner-contracts.log` |
| Complete Lambda emission controller | 97/294 | `final-emission.json` |

The emission controller includes 284 fixture cases: 87 pass, 197 fail. The
older 279 cases improved from 2 passing in the audit to 82. All five new
fixtures pass. These totals do not establish full corpus migration: remaining
cases include Full-specific result/parameter/register assumptions, changed
inlining and generic-path contracts, and missed MVP tunings. The remaining
inventory is in `remaining-emission.json`; review each contract before changing
it. At that checkpoint LR07-63 still failed compilation, and LR07-62 crashed
the release host. The apparent release success of LR07-61 checked only exit
status; the follow-up golden exposed malformed value output and fixes it below.

The ten optimization-contract failures remain: immutable array-consumer crash;
borrowed numeric store helper; recursive union admission work; union cache-hit
minimum; tail string copies; hyphen append work; checked-tail frame reuse;
read-only length root reloads; nullable packed construction; and redundant
stable-length reads. Python retains three failures: propagation boxing,
conditional modular slow calls, and JSON constructor fusion. The registered
runner reports those failures and exits nonzero. No case is skipped, marked
expected-failure, or removed from its selection.

The new argument-order golden also exposes a T0 snapshot discrepancy, LR07-65.
Keep `[3, 9]`; changing it to the interpreter's `[9, 9]` would hide S9.1.3v3's
plain-argument snapshot rule.

No Lambda/Radiant baseline or benchmark timing campaign ran. The private split
release build needed two Lambda-owned include dependencies exposed by the
concurrent header split. No sibling implementation/header/test or vendor source
was edited.

## Remaining-fixture follow-up (2026-11-10)

Starting HEAD: `c2f1d2426dcd8621db195c3d2293b9eafab90cf7`. Fresh private
release/controller qualification reproduced the 197/284 fixture failures.
Reviewed 156 contracts across result/parameter ABI, scalar homes, COW, numeric
lanes, arrays, record fields, strings, predicates, bounds and effect guards.
Native positive witnesses, checked misses and cost ceilings remain explicit;
retired Full registers, trampoline names and constructor assumptions do not.
The unresolved inventory includes both further contract review and real
missing scalarization, native wide/compact lanes, builders, TCO and guard
caching. No case is skipped or marked expected-failure.

Every Lambda MIR fixture now also runs against a platform-aware `.txt` golden
in the same controller. Twelve missing goldens are supplied. This closes the
hole where a script could exit zero with a wrong value while its MIR patterns
passed. Output corrections follow error propagation (S5.1.1v2–S5.1.2v2,
S7.9.2v3), contract validation (S11.4.1v5) and frozen receiver observations
(S9.1.2–S9.1.3v3). The persistent-record typed-boundary rejection stays intact;
an additional observation requires the preceding unannotated write to succeed
and its old COW snapshot to remain unchanged (D3.4.5, S11.4.10).

Named MIR sequences retry ambiguous prefixes with independent capture sets.
A store prefix cannot bind the registers needed by a later real load. Strict
adjacency still applies; bounded search fails explicitly if exhausted. A
positive/negative matcher unit covers retry, wrong-address/store rejection and
label adjacency. Shared JS named-capture fixtures qualify the same matcher.
The interval fixture also invokes its unbounded controls with `INT53_MAX` so
closed-call constant specialization cannot erase their required slow paths.

Compiler/runtime root fixes:

- Nullable integer-to-float conversion retains absence; already-guarded
  numeric operands use their present contract rather than duplicating guards.
- Runtime result publication resolves a pending wide scalar with its MIR
  companion before retaining the Item (D6.2.1). The earlier release exit-zero
  observation did not validate its malformed result text; the golden now does.
- Recursive inferred-array classification reads the current parameter proof,
  with declared-contract fallback for imported formals. It now selects the
  recursive extent proof without suppressing unplanned checked-store errors;
  effect verification remains enforced (D6.3.2).
- Mutable nullable records lose inferred layout proofs just like non-nullable
  records. Nested writes and projected `var` arguments are audited through the
  existing COW place-root helper. A primitive-array invariant borrow can
  preserve the root domain only when it borrows that root directly. Later
  typed crossings validate mutated records again (D3.4.5, S11.4.10).

Qualification artifacts are under `temp/mvp_fixture_followup/`; source and
controller identities are in `record-identities.json`. Final private release
SHA256: `cb1fac1e77f2b7ac74163f3be0c0b1734446c9289148deca629a738af11ee8ef`.

| Final unit gate | Result | Receipt |
|---|---:|---|
| Complete emission controller | 530/579 | `record-final-emission.json` |
| MIR emission contracts | 235/284 | same report |
| Semantic goldens | 284/284 | same report |
| Forced GC + poison, all goldens | 284/284 | `record-forced-goldens.json` |
| Seeded GC + poison, all goldens | 284/284 | `record-seeded-goldens.json` |
| Optimization contracts | 46/56 | `record-final-opt.json` |
| Lambda emission/frame ratchets + config | 18/18 | `record-final-ratchet.json` |
| Shared JS named-capture regressions | 8/8 | `final-js-regressions.json` |
| MIR interpreter root-fix goldens + record T0 parity | 5/5 | `record-backend-parity.json` |

All 49 remaining emission failures were already among the 197: 148 original
failures are resolved and no previously passing whole fixture regressed. The
ten optimization failures are unchanged. Budget objects are unchanged. The
strengthened record MIR/golden controls both reject the preceding engine and
pass the fixed engine (`record-negative-control.json`, `record-contracts.json`).
The four nullable/wide/recursive/record fix goldens match under native JIT and
the MIR interpreter; the record golden also matches T0. Imported scalar flow
passes in the complete golden population. Remaining fixture names are recorded
in `remaining-record-final.json`.

The shared `lambda.exe` remains unchanged. No sibling/vendor source edits,
Lambda/Radiant baseline, Python aggregate, timing campaign or commit occur in
this follow-up. Debug and non-macOS hosts are not requalified by these release
unit results. Full corpus migration remains incomplete until the retained
emission and optimization failures are fixed.

## Budget review (D8.6.1)

These are first MVP budgets, rather than a claim that old Full emission costs
were preserved. MVP's guarded native/checked bodies, inlining and precise
result/home ownership differ. The measured default budget is shared by the
identical darwin release/debug emission observations for the original twelve
probes; debug execution of the wide-tail probe still traps. Other platforms
receive no slack and need explicit qualification when their emission differs.
The seven JS probe objects are unchanged. Tests never write the budgets file.

| Existing probe | Previous Full default instructions | MVP instructions |
|---|---:|---:|
| Scalar home | 251 | 317 |
| Wide tail | 266 | 383 |
| Sized boxing | 57 | 114 |
| Closures | 2,350 | 5,073 |
| Float arrays | 1,849 | 3,839 |
| Nested COW store | 166 | 418 |
| Typed array guard | 1,461 | 1,681 |
| Call-site inference | 602 | 557 |
| Noncollecting libm | 541 | 695 |
| Cube3d | 19,072 | 23,903 |
| Deltablue | 14,646 | 22,557 |
| Prettier AST | 21,950 | 29,089 |

The five new region probes additionally pin every emitted function and its
roots, inserted root stores, scalar homes, number scratch and safepoints.
Numeric loops/divmod remain rootless with no scalar homes; array and record
misses retain their checked frames; closure and wide-value crossings retain
owned homes. These ceilings add zero slack to the observed MIR.

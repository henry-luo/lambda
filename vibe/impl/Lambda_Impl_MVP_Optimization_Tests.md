# MVP MIR optimization test migration

**Date:** 2026-11-10. **Starting HEAD:** `cf0caf5c7`, main checkout, `master`.

## Current checkpoint

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
- Runtime failures LR07-61/62/63 and copy-work failure LR07-64 remain genuine
  failing controls unless a root cause is fixed. They are not migration excuses.
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

## Delivered contracts and current results

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
it. LR07-63 still fails compilation, and LR07-62 still crashes the release host.
LR07-61 remains a debug-host failure; the rebuilt release executes that probe.

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

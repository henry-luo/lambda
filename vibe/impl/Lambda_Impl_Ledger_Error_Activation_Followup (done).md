# Ledger error and activation follow-up

**Status:** done for this bounded ledger pass; required Lambda and Test262 baselines pass.
**Date:** 2026-10-29. **Base:** `911b13d9f`.

This pass implements existing **S7.4.4 / S8.2.4v3 / S11.4.1v3 /
S12.3.4 / S12.3.6 / D1.4v4 / D5.1.1v3 / D5.3.3 / D5.3.6v2** rulings.
No semantics or design ruling changes. Earlier qualifications in the active
ledger remain historical checkpoints.

## Delivered behavior

| Record | Behavior and root cause |
|---|---|
| LR02-32 | An inline element subscript is parsed through the existing primary-type-slot parser, as query operands are. `e[<p>]` reaches the type-key query under S8.2.4v3. Bare tag-name policy is unchanged. |
| LR02-33 | A known builtin without a compatible registry row is E206 before execution. Re-resolve pre-bound singleton rows after counting arguments; preserve user shadows, qualified exports, pipe injection, and the existing S10.1.1v4 set-operand fold. |
| LR10-14 | Error members read only their own payload. A compatibility sentinel never reads ambient `last_error`. Integer conversion, formatted runtime errors, parser cleanup, glTF formatting and virtual writes retain owned diagnostics under S7.4.4; structural-equality depth exhaustion fills its existing explicit completion payload under D1.4v4. |
| LR10-15 | Deferred checks carry a lazy source descriptor. T0 and MIR keep source coordinates at declarations and caller-owned parameter admission; generated scalar, structural, binder, native-u32 and terminal-null admission paths share one runtime call. Resolve the source point only on failure, retaining successful admission fast paths under S11.4.1v3. |
| LR08-13 | Interpreted environments occupy exact side-root slots in their activation segments. Abandonment releases those roots without running a destructor. Realm-owned persistent global environments retain explicit registration; temporary scopes do not. D5.1.1v3 / D5.3.3. |
| LR12-43 | New stack lifetimes clear retired shadow state before priming. ASan receives start/finish fiber-switch notifications for normal, nested and base-stack transitions, with terminal fake-stack retirement. Cold-stack serialization preserves application poison while copying inactive physical frame bytes. Exact GC roots and native-stack scanning policy are unchanged. |
| LR10-17 | The original finite 3,072-frame native probe is already contained on this checkout. A permanent direct activation regression complements the existing unbounded recursion case, under S7.11.1v2 / S7.11.2v2 / D5.3.6v2. |

`runtime_error_itemf` is a private shared producer; it formats into its own
record before allocating the returned heap payload or replacing the diagnostic
mirror. Seventy adjacent diagnostic-plus-sentinel returns were migrated;
cleanup paths retain the returned payload across disposal. Array COW setters share
one rooted prepare/store implementation and propagate the actual error-tagged
completion, including payload-bearing errors; their previous exact-sentinel
comparison could silently publish a failed store as a success. This does not change
SO50's unresolved collection-comparison policy. Opaque legacy sentinels retain
stable default members; this pass does not claim that every legacy producer has
been given an operation-specific rich diagnostic.

The builtin arity fix also exposed a positive std fixture that had printed
`error` for `range(1, 5)`. It now uses the supported `range(1, 5, 1)` and verifies
its actual range. No unsupported overload is added.

## Existing repairs verified

**LR07-20**, **LR07-39**, **LR07-48** and **LR05-16** already pass the preserved
executable and current source. `ledger_verified_semantics.ls` covers imported
and earlier-parameter defaults, declaration-owned imported patterns, and sized
axis reductions under **S1.6 / S11.1.2v4 / S10.2.4**. These records receive
verification status, not attribution to this implementation.

LR12-31's row replacement succeeds on AUTO/JIT, while strict T0 rejects its
index-assignment node. It is not closed by this pass.

## Regression evidence and sanitizer controls

Permanent Lambda fixtures have paired expected files. The arity gtest checks
bare, qualified and piped invalid calls, with positive shadow and set-fold
controls. Native coverage checks parked generator/async root counts, legacy
sentinel identity and finite activation overflow. JS fixtures exercise repeated
async-stack reuse and forty simultaneously parked generators.

The pre-fix ASan host aborts on the async-reuse reproducer with the same
160-byte stack-use-after-scope write reported in LR12-43. Correct switch
bookkeeping also exposes ordinary memcpy's rejection of inactive compiler red
zones during cold-stack imaging. The narrowly uninstrumented physical byte
copy changes no application shadow: a native forty-activation control passes
with live locals and still reports a genuine use-after-scope after the first
cold activation resumes. `detect_stack_use_after_return=1`, forced GC and freed
memory poisoning stay enabled. No vendor code or Test262 harness changes.

The original Scene3D runtime and mesh event files pass with desktop access:
**22/22** and **17/17** assertions, respectively, with no ASan report. Initial
sandbox runs stalled in macOS input-service startup and are not source verdicts.

## MIR expectation migration

Generated admissions now call `lambda_type_check_lazy`, carrying the source
record and optional binder environment. Thirty-eight MIR sidecars update only
that helper/prototype spelling. Parsed-contract comparison preserves every
count, limit, other pattern, and description. Instruction budgets are unchanged.

## Final qualification

All receipts are under `temp/ledger-fixes2-20261029/`.

| Gate | Final result | Receipt |
|---|---|---|
| `make test-lambda-baseline` | **8420/8420**, including 2112 input and 6308 runtime checks | `lambda-baseline-qualified.log` |
| `make test262-baseline` | **40261/40261**, zero regressions | `test262-qualified.log` |
| Release scripts on interp/auto/jit, normal and forced/poisoned GC | **90/90**, including the previous ownership fixtures and all new arity/source-location negatives | `focused-final-release.json` |
| Native activation and concurrency-runtime suites | **34/34**, including finite/unbounded overflow, cold compaction and error-member independence | `native-activation-final.json` |
| Parked JS generator/async global-root regression | **1/1**; also included in the 252/252 JS script baseline | `parked-roots.json` |
| Existing COW value-flow and inferred-float-store tier coverage | **2/2** | `cow-tier-final.json` |
| MIR emission contracts / size ratchet | **279/279** / **20/20**; no count or instruction-budget changes | `mir-final2.json`, baseline log |
| ASan JS reuse and parked-wave stress, AST and MIR | **4/4**, forced/poisoned GC, stack-use-after-return enabled | `asan-final-js-results.json` |
| ASan original Scene3D runtime / mesh live event files | **22/22** / **17/17** assertions, no ASan report | `asan-final-desktop-results.json` |
| ASan cold-stack controls | Live locals pass; expired scoped-local access still produces the expected ASan failure | `asan-native-final-controls.json` |

The unrestricted auxiliary `test_lambda_concurrency_gtest` runner is **not a
passing aggregate receipt**: it fails the Jube ordinal/husk parity case, then
shows context-switch refusals and crashes during script-cache tests before
reaching the selected activation suites. In fresh isolated processes, the
Jube parity case still fails, while realm identity and fresh-runtime script
cache reuse pass (`native-unrelated-isolated.json`). The complete activation
and concurrency-runtime suites pass together at 34/34. This pass does not
claim `make test-lambda-full` qualification or resolve the auxiliary runner's
Jube/context-lifecycle failures.

The first required Lambda run exposed error payloads discarded by exact-sentinel
COW comparisons; the shared rooted setter fixes that cause. Its final rerun is
fully green. The final source changes after that receipt only initialize/release
the native error regression's missing name pool, adjust a comment, and correct
the new qualified-arity golden to the runtime's canonical builtin name `len`.
The native suite and release fixture checks were repeated after those changes.
No expectation for an existing failing engine behavior was weakened.

All eleven resolved records are in the fixed ledger: six new repairs and five
verified existing repairs. S7.4.4 remains partial for opaque compatibility
producers; RA-O2's general owner inventory, LR12-31's strict-T0 index-assignment
coverage and pending-ruling policies remain open. No Radiant layout aggregate
was run because this pass changes no Radiant source. The original live ASan
reproducers are explicitly qualified above.

`make formal-index` regenerated an unchanged index after the implementation
status footnote changed; its existing duplicate-DO25 warning is unrelated.
Local links/new ledger anchors, paired Lambda goldens, parsed MIR contracts
and `git diff --check` pass. Preserved pre-change executable provenance is in
`before.json`; final executable and source hashes are in `final-provenance.json`
and `source-manifest.json`. No commit or broad staging was performed.

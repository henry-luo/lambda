# Failed Radiant integration test repair — 2026-11-11

Authority: **D9.1.1v4 / D9.1.3v2**; preserve **D5.5 / D6.3** through the
unchanged native host interfaces. The existing Makefile quiet-build edit is
user-owned and remains untouched.

## Delivered native repair

The earlier RAD15-5 logging cleanup removed a user-facing replay-output contract.
With disabled or filtered logging, the seven view UI fixtures and many view
GTests could not find `Assertions:` / `Result:` even when the machine result
reported success. `event_sim_print_results` again emits its formatted human
summary independently of logging. It uses the project string buffer and a
buffered output write; diagnostic lines retain the logging API. Neither replay
assertions nor JSON counts are weakened.

The new native subprocess regression covers passing, failing and zero-assertion
summaries under disabled logging, alongside the unchanged JSON result schema.
Its human-output assertions fail on the original source and pass after the
repair. Original failed reports are copied under
`temp/radiant-failed-20261111/original/`; focused proof uses
`result-contract-{before,after}.{json,log}`.

An isolated current-source integrated host links the repaired archive. The exact
reported inventories pass **326/326 UI cases** and **278/279 view cases with one
existing speech skip**, including the original summary failures. The temporary
workspace was corrected to include the read-only doc/AGENTS/Makefile/lambda
fixtures and preserve tests' explicit log paths; intermediate setup failures
are retained separately and are not source regressions.

The final owner gate `make -j8 INPUT_ROOT=/Users/henryluo/Projects/lambda-input
run-radiant-baseline` passes **894 native tests plus static smoke**, **7 runner
contracts**, **3,851 static and 630 dynamic geometry comparisons**. One hardware
MIDI case and six static fixtures retain their existing skips. Layout lint
checks 89 casts with zero unmarked; its five contracts, input-facade and diff
checks pass. The normal Lambda executable is refreshed through its documented
split build so future user runs consume the native fix.

## Delivered Lambda runner repair

The shared layout harness defaults to `radiant.exe`; Lambda's isolated test
workspace provisions its own `lambda.exe`. Neither `run_layout_baseline.js` nor
the page snapshot adapter selected that host explicitly, so every layout batch
failed with ENOENT. The approved two-file
[patch](../../radiant/temp/radiant-failed-20261111/lambda-runner-fix.patch) selects the
existing Lambda host. No runner inventory, scores, references or tolerance is
changed.

The patch was first tested in
`temp/radiant-failed-20261111/lambda-runner-proposal/` without changing sibling
source. The page snapshot passes **51/51** using its existing average/integrity
contract; it still reports permitted individual fidelity differences. All
**5,936** combined layout cases execute: **5,621 pass, 293 partial, 10 fail and
12 skip**. The ten remaining comparisons are recorded as [RI-010](../Radiant_Issue_Ledger.md#ri-010),
with original and fresh-browser evidence. Two stale fragment captures and
standards-versus-browser differences must not be hidden by a native patch or
baseline change.

The user explicitly approved the two-file ownership exception, and the patch
is applied to Lambda. The final normal integration entry-point run is recorded
in the receipt; RI-010 keeps its ten remaining comparisons visible. No broader
Lambda implementation, test or build change is made.

The final normal Lambda shared-runner invocation selects the five reported
groups through `test/test_run.js --target=radiant --category=baseline
--radiant-baseline --lambda-owned --include-test=... --sequential`. It confirms
**326/326 UI**, **111/111 page-load**, **51/51 page snapshot**, and **278 view
passes plus one speech skip**. The combined layout group retains the same
**5,621 passed / 293 partial / 10 failed / 12 skipped** result. Thus this
five-group run exits one for RI-010, while the independent owner-native gate
passes. The final reports and all intermediate failures remain under
`temp/radiant-failed-20261111/`; the native fix and applied Lambda patch are
qualified without claiming the remaining layout gate green.

The pinned source, host, reports and verification status are listed in
[the receipt](../../radiant/temp/radiant-failed-20261111/verification-summary.json).

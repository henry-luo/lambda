# MVP MIR optimization test migration

**Date:** 2026-11-10. **Starting HEAD:** `cf0caf5c7`, main checkout, `master`.

## Current checkpoint

- User requested migration of obsolete MIR optimization tests and new MVP
  emission coverage. The preceding audit is under
  `temp/mvp_optimization_coverage/coverage-report.md` (LR07-60).
- Preserve intended native/checked behavior and exact cost review; do not
  suppress implementation failures or restore retired Full helpers (D8.6.1,
  D6.3, S9.1.2–S9.1.3).
- Existing unrelated changes move runtime declarations into Lambda ownership.
  Their initial diff/hashes are saved under `temp/mvp_emission_test_update/`.
  Preserve those edits; this task owns test/config changes and the narrow MVP
  test-injection seam. Sibling and vendor sources remain read-only.
- Planned work: explicit MVP function/callee selectors; reviewed helper/path
  assertions and frame budgets; robust telemetry controls; focused Python gate
  registration; small typed/untyped fixtures covering native paths and misses.
- Runtime failures LR07-61/62/63 and copy-work failure LR07-64 remain genuine
  failing controls unless a root cause is fixed. They are not migration excuses.
- Build private controllers/engine as needed, preserve shared `lambda.exe` and
  run unit gates only. No Lambda/Radiant baseline or timing campaign.
- Next: update the shared matcher/executable selection and explicit selectors,
  then inspect assertion mismatches by optimization family before revising them.

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

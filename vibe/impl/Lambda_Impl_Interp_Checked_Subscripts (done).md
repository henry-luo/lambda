# Interpreter checked subscript admission

Date: 2026-11-11. Starting tree: `8b4fbd521`, main checkout, `master`.

## Completion record

- Goal: repair the reported interpreter-only plain-golden E501 failures.
- Reproduced 37 E501 failures in 2,215 discovered goldens, plus four sandbox
  networking failures. The original report counted 38 E501 failures.
- Implementation is complete. A rebuilt debug host passes all 2,216 goldens
  under explicit `interp`, with loopback access and no E501 failures.
  Receipt: `temp/interp-e501/after.xml` and `after.log`.
- Ten representative fixtures pass on interp, auto and jit, normally and with
  force-every/poisoned GC. The new procedural fixture checks coordinate order,
  a later allocating coordinate, snapshots, rejected stores, `last` nesting and
  computed string/symbol keys. Receipts: `admission.xml`, `admission-gc.xml`.
- Walker/frame and strict-fallback checks now pass 53/53. The first run found two stale tests:
  `TypeBinderContractsAgreeAcrossAllTiers` pins an old raw-variant result and
  its negative source now fails E228 before reaching its intended admission;
  `ImmutableProcAliasWithTaskStaysPinned` expects rejection of a task T0 runs.
  The saved original executable reproduces the underlying observations. The
  assertions now use the canonical golden, propagate the negative call so it
  reaches runtime, and check task execution under D8.3.2. A separate genuine
  unsupported-receiver case preserves strict rejection coverage (D8.3.1).
  Evidence: `walker.log`, `binder-before.out`, `binder-nested-before.err`,
  `task-before.err`, all under `temp/interp-e501/`.
- The 1,549-row differential census reports 1,543 matches and six differences,
  so regeneration refuses to replace the coverage lists. Five stable JIT
  differences reproduce unchanged on the saved original host; one fault case
  matches in isolation. Recorded as [LR07-66](../Lambda_Issue_Ledger.md#lr07-66).
  Full-census success and refreshed lists are not claimed.
- `make test-lambda-baseline` passes 7,418/7,418, including the main Lambda
  GTest's 2,306/2,306 cases. Receipt: `temp/interp-e501/lambda-baseline.log`.
  The final debug host is saved as `temp/interp-e501/final-debug.exe`, SHA256
  `debf2b64c2646bc6268cc139fad79ecc5426cd1330709dda21f6f5220ec4504a`.
- The first Test262 release build stopped on unused diagnostic counters in
  Lambda's `radiant_paint_adapter.cpp`: `log_debug` disappears under `NDEBUG`.
  The counters now carry C++17 `[[maybe_unused]]`; their debug behavior stays
  intact. This is the only change after the passing debug baseline and changes
  no execution behavior. The original compiler diagnostic is retained in
  `temp/interp-e501/release-build-before.log`.
- `make test262-baseline` passes 40,261/40,261, with zero regressions or unstable
  cases. Receipt: `temp/interp-e501/test262-baseline.log`.
- Final release cross-tier fixtures pass normally and with forced/poisoned GC;
  walker/frame/strict-rejection checks pass 53/53 on that host too.
  Receipts: `admission-release.xml`, `admission-release-gc.xml`,
  `walker-release.xml`. The final release is `temp/interp-e501/final-release.exe`,
  SHA256 `97603dcb9563039ef8277dfaa38a5d66a2069bb32933a3f250ab19a5cfcb5bc5`.
- Source diff checks pass. No sibling source files were changed (D9.1.1v4,
  D9.1.3v2). No Node or Radiant baseline was run. No jobs remain running.
  The E501 repair is complete; independent census issue LR07-66 remains open.

## Causes and repair

The admission scan required literal integral coordinates for N-D reads and a
small syntax proof for single-key writes. Their evaluators already use checked
runtime decoding and setters. Calls, concatenations and dynamic coordinates
were rejected before execution, including in imported DocBook and EPS modules.
Admission now checks the coordinate-buffer bound and preserves runtime key
validation (S8.2.1v4, S8.2.4v3, S7.1.3v3). Strict interpreter mode still cannot
fall back to MIR (D8.3.1).

The remaining stores required evaluator repairs. N-D writes now detach shared
owners and publish replacements, preserving snapshots (S9.1.2). Direct stores
bind `last` to their own rooted owner and reload the owner after evaluating a
key that could replace it (S7.2.2). Both read and store scratch plans cover every
coordinate expression, including later coordinates (D6.3.1).

An explicit method receiver store, `~.field = value`, now resolves the same
field binding as a bare assignment. Both spellings share assignment admission
and publication. Field lookup is shared with MVP; nested bodies that replace
`~` remain outside this method-store path (D3.1.4, S12.3.3v2).

These changes implement existing rulings. They change no language ruling,
golden oracle, vendor source, or sibling project source.

# Workspace transactions and ZIP replication

**Date:** 2026-11-11
**Design:** [CRDT protocol §10](../Lambda_Design_CRDT_Protocol.md#10-workspace-transactions-and-zip-workspaces)
and [data sync §6](../Lambda_Design_Data_Sync.md#6-directory-and-file-synchronization).
**Spec linkage:** S12.1.1v3; S12.4.1v3/S14.3.1v3; S2.2.4; D9.5.5.

## Completion record

Completed 2026-11-11 on branch `master`, based on HEAD
`21fdc7cf6bffa8828a7b6749d97d8e1e0b3b020a` plus the uncommitted changes
identified by `temp/sync-workspace/qualification-inputs.json`.

- Implemented atomic workspace transactions and ZIP-backed namespaces in five
  Lambda Script modules, with shared core/auth/transport/restart integration.
- Workspace revisions include workspace/epoch scope; moves retain cycle-safe
  replay history, tombstones, explicit restoration and recoverable offline work.
- ZIP handling captures member identities/content, bounds actual wire batches,
  preserves authored groups, merges external captures from the disk baseline,
  guards saves and retains review/recovery state across restart.
- Fixed the Lambda-owned imported-procedure caret carrier bug exposed by ZIP
  saves (D4.3.1/S7.7.2v2); no vendor or sibling source changes were made.
- Verification passed: full baseline **7,437/7,437**, focused normal **32/32** on
  each tier, full isolated GC/poison sweeps, final workspace reruns, and rebuilt
  binary normal/GC rechecks. Detailed scope, artifacts and commands follow below.
- Existing CRDT/TeX and concurrent unrelated runtime edits were preserved.
  No staging or commits were performed. No work remains for this prototype scope.
- Host filesystem CAS/locking and materialization journals, deployed transports,
  repository encryption/physical GC and editor binding remain separate integration
  work under the design contract. Same-epoch namespace history remains retained.

## Milestones

1. Design contract and implementation checkpoint — done.
2. Namespace events, move replay, lifecycle and saved revision heads — done.
3. Atomic command groups, protocol/restart and durability integration — done.
4. ZIP capture, export/save and external-branch reconciliation — done.
5. Qualification, documentation and completion record — done.

## Implementation notes

The workspace event descriptor extends the existing core without changing the
Text/Mark ownership model. `workspace_rules` is independent of the core and can
validate both incoming operations and reconstructed compacted events. Namespace
moves replay in total logical order and retain suppressed intent; metadata packet
compaction does not erase move history.

ZIP import forks from the accepted disk baseline, then joins the live endpoint
with its authored transaction journal. Internal baseline records use a fixed
field order for hashing, while user Mark map order retains S2.3.1v2 semantics.
Captured input stores member bytes even when identity mapping needs review.

The native JIT reduction is in `temp/sync-workspace/_jit_import_record.ls` and its
helper. The caller inferred a map while the body ABI returned an erased Item.
Passing a null source to `propagated_value` lost the caller's success proof after
checking Error. Passing the actual call AST reuses the established discharge and
carrier conversion; no additional receiving contract or test workaround is added.
LLDB evidence: `jit-binding.log`; final C++ delta versus this task's starting
source: one changed call plus its root-cause comment. The helper and procedural
regression check both success and error recovery.

The 2026-11-11 ledger follow-up registers this fixed compiler defect as
[LR07-67](<../Lambda_Issue_Ledger (fixed).md#lr07-67>). The independent
`file`-binding probe is added to the existing open report in the
[active issue ledger](../Lambda_Issue_Ledger.md); its cause remains unconfirmed.

## Verification

The final source consists of 19 sync modules, 11 new workspace/ZIP fixtures,
and one imported-procedure regression plus its module helper. All new executable
fixtures have goldens. No fixture or harness was weakened to obtain a pass.

| Gate | Result / evidence |
|---|---|
| Full Lambda baseline | **7,437/7,437**, all 40 suites; `temp/sync-workspace/baseline.log` |
| Runtime golden/unit runner | **2,326/2,326**, included in baseline |
| Native MIR forced-GC gate | **368/368**, included in baseline |
| Focused normal tiers | **32/32** each on interpreter, AUTO and JIT; final rebuilt logs `*-rebuilt-normal.log` |
| Full isolated GC/poison sweep | **32/32** each; `final-qualification.log`, per-tier stress summaries |
| Final workspace GC/poison rerun | **11/11** on interpreter/AUTO after revision scoping and delete/create handling; JIT full sweep read final source |
| Rebuilt-binary GC/poison recheck | **6/6** on interpreter/AUTO/JIT; `rebuilt-qualification.log` |
| Package structure / UI boundaries | **854** scripts; **37** entries / **43** dependencies, included in baseline |
| Independent ZIP reader | Python `zipfile` checks CRCs, renamed/edited JSON, all unchanged original member bytes, empty entries and opaque binary bytes |

Focused normal command (set `LAMBDA_EXEC_BACKEND` to each tier):

```sh
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/sync_*:AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/proc_sync_workspace*:AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/proc_propagated_import_record'
```

Isolated stress uses the existing `test-batch --no-log --timeout=300` CLI,
`LAMBDA_GC_FORCE_EVERY=1` and `LAMBDA_GC_POISON_FREED=1`. The driver checks the
complete batch markers, exact golden, exit status and teardown. Reference bundle
limits and array-based implementation costs remain documented in the package.
No release-performance claim is made from these debug qualification runs.

The first qualified debug_native executable SHA-256 was
`68b0ae99547128a89e7bd034fa57e097d14d80894ac3d10ad4f834fc7ff11c38`.
The final baseline rebuild produced
`e5860fd168129a1c2a82989b15412311512d5cd7859f20a61633215021e6cabf`.
Normal tiers and six authentication/move/lifecycle/wire/ZIP/native-regression
stress fixtures were rechecked after that rebuild. Earlier candidate logs are
retained but superseded by these final checks. This task's native source delta
remains the one call-site propagation change; unrelated concurrent runtime and
TeX work was preserved. The tested HEAD and relevant uncommitted source/golden
hashes are in `temp/sync-workspace/qualification-inputs.json`.

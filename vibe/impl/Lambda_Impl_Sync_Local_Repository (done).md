# Local sync repository and filesystem integration

**Date:** 2026-11-11
**Status:** Complete; experimental native local milestone implemented and qualified.
**Design:** [Data sync §§5–6, 10 and 12.1](../Lambda_Design_Data_Sync.md),
[workspace protocol §10](../Lambda_Design_CRDT_Protocol.md#10-workspace-transactions-and-zip-workspaces).
**Spec linkage:** S12.1.1v3 (pure algorithms), S12.4.1v3 (captured input),
S12.4.4v4 (IO effects), S2.3.1v2 (map order), D9.3.4 (packages),
D11.1.2 (central IO), D11.2.1–D11.2.4 (host grants).

## Completion record

Completed 2026-11-11 in `/Users/henryluo/Projects/lambda`, branch `master`, on
HEAD `21fdc7cf6bffa8828a7b6749d97d8e1e0b3b020a` plus the uncommitted inputs recorded
in `temp/sync-local/qualification-inputs.json`. Existing CRDT/TeX work and
concurrent unrelated changes were preserved. No staging or commits were performed.

- Delivered encrypted immutable object files, a rebuildable SQLite locator catalog,
  encrypted generation-checked local heads, Gear chunking/deduplication, full disk
  captures, move-aware reconciliation, crash-recoverable installation intents,
  file history, saved document checkpoints, snapshots, verification and restore.
- Policy is primarily Lambda Script: six new modules, shared path overlap handling,
  and a byte-based typed-codec preflight. ZIP and directory workflows share the
  extracted captured-workspace algorithm instead of duplicating it.
- Native additions are a 169-line anchored disk bridge and 68 added crypto lines,
  plus registry/import wiring. mbedTLS supplies AES-256-GCM/HMAC-SHA256 and entropy;
  existing file anchors and SQLite supply filesystem/transaction mechanisms.
- Eleven new executable fixtures have goldens; one `mod_*` helper owns the
  explicitly trusted local exchange used in tests. No test harness was modified.
- Fixed and archived LR07-68, LR12-51 and LR12-52 with root-cause evidence.
  LR07-69, the existing missing general dynamic builtin-call adapter, stays open.
  Direct native APIs are qualified; no per-crypto ABI exception was introduced.
- Final Lambda baseline: **7,458/7,458**, all 40 suites; runtime **2,347/2,347**;
  native MIR GC **368/368**. Normal coverage: **44/44** on interpreter/AUTO/JIT.
- Isolated every-allocation GC/poison: **7/7** selected cases on JIT and **4/4**
  on interpreter/AUTO. The baseline rebuild then passed **4/4 normal + 4/4 stress**
  on each tier. Complete batch markers, goldens and process teardown were checked.
- The original combined filesystem scenario passed on AUTO/JIT but exceeded the
  usual test budget. Offline convergence and binary/empty-file fidelity are now
  separate fixtures; both pass on all tiers. Its interrupted old interpreter run
  is superseded. Acceptance assertions are preserved across the focused fixtures.
- Qualified ordinary debug host SHA256:
  `1a1148655d2698808e9a5111419461873dfc41cc2450e3b9fda805b980abec44`.
  Commands, evidence and remaining limitations follow below. No release performance
  claim or power-loss durability claim is made.
- No work remains for this local milestone. Deployed direct transports, cloud
  publication/polling, key enrollment, editor bindings and physical pruning remain
  later milestones, not completion claims for Data Sync §12.1 as a whole.

## Implementation and qualification

Public API and format contract: [native local repository](../../lmd/package/sync/LOCAL_REPOSITORY.md).
The new modules are `chunks`, `repository`, `filesystem`, `installation`, `local`
and shared `workspace_capture`. The unchanged document/workspace model supplies
stable identities, cycle-safe move replay, revision parents and atomic groups.
Local publications preserve their document frontier; later live edits remain
outside that saved file version.

Physical `LRP1` records hold authenticated encrypted objects independently of
SQLite. Rebuilding locators scans verified files; an interrupted transaction may
leave an unindexed object, which recovery exposes without advancing the live head.
Private local-state records are excluded from peer object exchange. Namespace
history and content alternatives remain retained; no destructive pruning is added.
A new writer prefix on every reopened handle prevents cloned/rolled-back state
from silently resuming an old actor stream.

Installation is journaled before effects. A repeated step recognizes already
installed content, checks accepted preimages and readback, and retains detected
external edits in encrypted recovery records. Captures distinguish an absent root
from an empty tree. Type changes require an explicit branch matching the capture;
competing file revisions require a new resolving publication. Idle scans avoid
head publication. Existing SQLite barriers qualify transaction/restart behavior;
its documented power-loss barrier remains unqualified.

Verification commands and receipts:

```sh
mkdir -p temp/sync-local
TMPDIR="$PWD/temp/sync-local" make test-lambda-baseline
./lambda.exe run test/lambda/proc/sync_local_filesystem.ls
./lambda.exe run test/lambda/proc/sync_local_binary.ls
./lambda.exe run test/lambda/proc/sync_local_recovery.ls
```

| Gate | Result / receipt |
|---|---|
| Full Lambda baseline | 7,458/7,458; `temp/sync-local/baseline.log` |
| Runtime tests | 2,347/2,347, included in baseline |
| Native MIR GC | 368/368, included in baseline |
| Normal three-tier coverage | 44/44 per tier; `normal-coverage.json` selects the final/split fixture receipts |
| Isolated GC/poison | 7/7 JIT; 4/4 interpreter/AUTO; per-tier `*-stress-summary.log` |
| Rebuilt-host check | 4 normal + 4 stress per tier; `*-post-build-*-summary.log` |
| Package/import structure | 862 shipped scripts; included in baseline and checked independently |
| Crypto boundary | NIST GCM/RFC 4231 vectors, empty bytes, wrong key/context, tamper and truncation |
| Storage/recovery | Dedup/reconstruction, stale generation, retained offline conflicts, orphan/index recovery, corrupt/missing objects, snapshot restore |
| External changes | Rename plus edit, save-by-replacement, explicit type replacement, failed/symlink scans, blocked install and restart |
| Documents | Saved Text checkpoint resolves through its keyed repository ID; later unsaved edits remain outside the saved frontier |

Input hashes, source commit/configuration and executable identity are in
`qualification-inputs.json`. The temporary qualification driver checks unchanged
fixture goldens in isolated `test-batch --timeout=300` processes. The native
builtin-reference probe and its E229 result remain in `native-first-class.ls` /
`native-first-class-jit.log` under the same evidence directory (LR07-69).
The separate LR01-24 timeout cleanup defect remains open; these successful
bounded runs do not qualify the cancellation/timeout path.

## Finite milestones

1. **Done — native boundary and repository format.** Add narrowly scoped authenticated
   encryption/key generation/keyed digest APIs if absent, using vetted native
   crypto. Add only missing anchored filesystem capture/install APIs. Pin the
   algorithm/profile and define authenticated object identity/context. Record
   runtime defects in the central Lambda issue ledger; respect sibling ownership.
2. **Done — encrypted immutable local repository.** Implement chunking, scoped
   deduplication, manifests, encrypted objects, transactional head publication,
   restart, saved workspace/checkpoint links and permanent version roots in
   Lambda Script. Keep object encoding independent of SQLite/cloud locations.
3. **Done — filesystem capture and reconciliation.** Reuse workspace moves, tombstones
   and revision conflicts. Track accepted disk baselines separately from live
   state, merge external changes as branches, and use verified identity evidence
   for renames. Bound scans and reject unsafe namespace mappings.
4. **Done — installation, exchange and recovery.** Journal desired installation before
   effects, preserve encountered external changes, resume after interruption,
   transfer missing authenticated objects using the existing metadata protocol,
   verify/rebuild derived state and restore history into a separate target.
5. **Done — qualification and documentation.** Add goldens for all executable fixtures,
   a runnable two-replica example, package/API documentation and final evidence.
   Update this checkpoint at milestone boundaries and document remaining limits.

## Acceptance cases and verification budget

- Crypto: round trip, independent known vectors, wrong key/context, tampered and
  truncated ciphertext; no plaintext fallback. Portable records bind version,
  object kind, space scope and logical identity.
- Repository: repeated content deduplicates, insertion retains reusable chunks,
  empty/binary files reconstruct byte-for-byte, immutable-ID reuse rejects,
  wrong keys and missing/corrupt objects fail explicitly.
- Durability: dependencies and head commit together; stale generations reject.
  Restart retains actor/counter state and pending work without ID reuse. Inject
  interruption before publication and during installation using protocol steps.
- Reconciliation: offline create/edit/rename/move/delete; competing replacements;
  move plus edit; identity ambiguity; save-by-replacement; failed/missing-root
  scan never publishes deletion; repeated scans/installations are idempotent.
- History: both conflicting revisions remain reachable, snapshots survive moves,
  verification detects damage, and a selected version restores independently.
- Run focused normal interpreter/AUTO/JIT tests after each milestone. Run bounded,
  isolated forced-GC/poison cases for native ownership and representative script
  flows. Run the full Lambda baseline once at the final implementation milestone.
  No debug-build performance claims or repeated broad suites without cause.

## Follow-on work

Direct LAN/remote peer transport, provider-neutral object-store publication and
serverless periodic polling come after this local foundation. Editor adapters
then bind Text/Mark transactions and external-edit warnings to live UI sessions.
Retention thinning, fenced physical reclamation and measured chunk/pack tuning
remain later milestones; this phase keeps all committed history.

## 2026-11-14 baseline observation

The Lambda baseline run after rebuilding current Input and Radiant passed
7,481/7,510 tests; 29 `proc_sync_*` tests failed. The isolated
`./test/test_lambda_gtest.exe --gtest_filter='*proc_sync_local_repository'`
also failed with E407, reporting unexpected JSON content at column 8192 while
reading a repository record. Root cause and ownership are unconfirmed; the
observation does not establish that all 29 failures have the same cause.
Evidence: `temp/edit-viewer/markdown-final-lambda-baseline.log` and
`temp/edit-viewer/markdown-sync-control.log`. D9.3.4 covers document parsing
and S7.5.1v6 covers acknowledgement of parse errors.

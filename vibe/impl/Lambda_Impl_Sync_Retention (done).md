# Paged history and repository maintenance

**Date:** 2026-11-12
**Status:** Completed for the bounded native reference profile; qualification residue below.
**Design:** [Data sync §§10.2–10.5, 12.3](../Lambda_Design_Data_Sync.md).
**Spec linkage:** S12.1.1v3 (pure policy and procedural effects), S12.4.4v4
(transaction ownership), D11.1.2 (IO boundary), D11.2.1–D11.2.4 (host grants),
D9.3.4 (source packages), D9.1.1v4 / D9.1.3v2 (project ownership).

## Completion record

- Completed 2026-11-12 in the main Lambda checkout, branch `master`, starting
  at `bbddec0f0` (input `ec1138d`), with this phase's uncommitted script/tests/docs.
  No native, vendor or sibling source changes. Concurrent LaTeX work is untouched.
- Delivered immutable paged saved endpoints, authored save/revision timestamps,
  permanent/daily/tiered retention, pins, monotonic retirement, protected roots,
  fenced local pruning, cloud copy/cut/prune/reap and verified cache hydration.
  Periodic owner-client ticks and a private committed-plan server entry provide
  bounded scheduling. Missing catalogs fail closed with explicit backup recovery.
- All three execution tiers pass 63 sync fixtures, six focused GC/poison goldens
  and 70 independent native-process checks each. Critical cases were repeated
  after the baseline's CLI rebuild; script/fixture identities stayed unchanged.
- Aggregate baseline: **7,542/7,543**; the unrelated LaTeX digest timeout passed
  its isolated unchanged-deadline retry in 58.7 seconds. Initial concurrent JIT
  document SIGKILL did not recur in the complete isolated 18-case rerun; cause
  remains unconfirmed. Neither aggregate pass nor a runtime fix is claimed.
- Six new golden fixtures contain 103 assertions. API/design/test docs and the
  central issue ledger are updated; evidence lives in `temp/sync-retention/final/`.
  `completion.json` records the matrix and exceptions; `inputs*.json` record
  identities. Rebuilt input-manifest SHA-256:
  `3f95237a6dbb6b2fca3ea24c85aebda44c5cd542d7f9c10a3ea3ea86be1591d7`.
- Owned backend PID 59216 was stopped after qualification; S3 port 65510 is
  closed. `cleanup.json` retains ownership/cleanup evidence. Test data/logs remain
  under `temp/`. No changes were staged or committed during implementation.
- Remaining scope is explicit below: retained metadata/anchors, audited
  legacy/recovered catalog cleanup, workload capacity, hosted/physical-device
  qualification and finer grants. LR01-24 and LI-015 remain separate open issues.

## Finite milestones

1. Paged immutable saved endpoints and ancestry; migration, bounded listing,
   remote transfer and checked exact restore, retaining live/base state separately.
2. Pure calendar selection with explicit zone, save/arrival/deletion grace,
   permanent/daily/tiered policy, pins and protected concurrent heads.
3. Local retention manifests, compact retained representations, resumable plans,
   root reachability, verification/rebuild, stale-plan rejection and cache eviction.
4. Per-space conditional cloud catalog, immutable physical generations,
   key-holder maintenance checkpoint, publication barrier, resumable bounded
   deletion, retirement propagation and fresh-device recovery.
5. Qualify offline return, late saves, snapshots/attachments, races and crashes;
   update API/design docs and record precise limits and evidence.

## Acceptance and verification budget

Focused pure and procedural goldens cover each milestone on AUTO first. Run the
sync fixture set on interpreter/AUTO/JIT at integration, plus focused GC/poison.
Process tests cover independent repositories, actual local S3, interrupted
maintenance, publication/retirement races and fresh recovery. Run the aggregate
Lambda baseline once at the final milestone; native/owner gates only if those
sources change. Keep ordinary deadlines and goldens intact. Use release builds
for any performance measurements; none are planned for this reference phase.

## Implementation and safety boundaries

The new modules are `history`, `retention_policy`, `retention`,
`maintenance_state`, `maintenance`, `maintenance_cli`, `reachability`,
`cloud_catalog`, `cloud_maintenance_function`, `cloud_maintenance`,
`maintenance_runner`, `cache_state` and `cache`. Existing repository, local,
document, peer and cloud modules integrate those operations. Native sources and all owner sources remain unchanged. The aggregate baseline
force-rebuilds the CLI entry point; old/new executable identities are recorded
separately, with critical checks repeated on the rebuilt host. User entry points and complete bounds
are in [MAINTENANCE.md](../../lmd/package/sync/MAINTENANCE.md).

The replacement endpoint is independent, rather than an A-to-B delta chain.
Compaction preserves same-epoch identities and arbitrary admitted offline work;
raw files whose old base payload expired retain competing heads. No replayable
per-edit archive or metadata epoch retirement is claimed. Namespace, ancestry,
arrival and retirement metadata still consume bounded envelopes. Paged storage
is not a production scalability/performance qualification.

Every cloud head commit and cut shares a conditional catalog. Uploaded keys are
reserved before capabilities; replacement bytes use a fresh physical epoch.
Paused old workers can affect only old immutable keys. Active publication
reservations have no automatic lease takeover; their writer must resume/cancel.
Cloud replacement currently recopies all retained objects. No cost/throughput
claim is made. Catalog-loss recovery requires an explicit known immutable backup
with existing validated dependencies; automatic discovery and inventory audit
are not implemented. Legacy/recovered stores fail closed for destructive work.
Old handlers/writers must drain/upgrade before enabling the new protocol.

Local file effects occur after a real pending SQL head write obtains writer
ownership. This follows [SQLite deferred transaction behavior](https://www.sqlite.org/lang_transaction.html),
not a claim that opening a scope itself owns the writer lock. Crash testing
covers process death between deletion and commit; arbitrary power-loss durability
retains the prior local-host qualification. Orphans retain their own dependency
context until explicit review/release, and verified cache intent distinguishes
missing cold bytes from unexplained damage.

Tests use four separately enrolled native devices, random isolated S3 prefixes,
periodic HTTP functions and actual encrypted object IO. This does not qualify
hosted clouds, containers/Linux, physical multi-device traversal, browser/GUI
integration, fine sharing/key rotation, or generic concurrent administration.
The existing full-space writer/checkpoint attestation remains (D11.2.1–D11.2.4).
Issues found in this phase and their reproductions are recorded under
[sync retention safety](../Lambda_Issue_Ledger.md#sync-retention-safety).

## Qualification

The six new exact-golden fixtures contain 103 Boolean assertions: policy 13,
repository 16, history 18, cloud fencing/recovery 27, offline return 17 and
small GC/storage boundary 12. All 63 discovered sync fixtures pass on AUTO,
interpreter and JIT. All six focused every-allocation GC/poison goldens pass on
those tiers at the existing 300-second stress budget. The optional large stress
profile remains unqualified; normal discovery deadlines and existing goldens
are unchanged.

The real-process matrix has 70 checks per tier: 23 retention, 18 documents,
12 peer and 17 cloud. All tiers passed; JIT documents/peer/cloud use the isolated
rerun after the concurrent-run failure described below.
Retention checks include partial replacement copy, dropped committed-cut
response, actual old-bundle GET failure after pruning, fresh recovery, retained
snapshot restore, cross-device deferred-cache hydration, offline unsaved return,
a writer rejected during deletion, process death before SQL commit, resumed
cleanup, bounded scheduled catch-up and non-resurrection of released snapshots.

Initial gates used native SHA-256
`50c31d9ed3062d6b270b5e361acc202b5d6c1a6df568658ae0588b64d00a1d92`.
The aggregate target force-rebuilt only the CLI entry-point objects, producing
`26ce0db31217420a68ffd130e9f604426a64161bf2cb8c1c5f48d99df56d4fb2`.
No native source was changed for this phase. Exact fixture, helper, package and
executable identities are retained in `temp/sync-retention/final/inputs.json`
and `inputs-rebuilt.json`; only `lambda.exe` differs. On the rebuilt host,
interpreter/JIT sync sweeps and the small retention GC golden on all tiers pass.
AUTO/interpreter real retention repeats also pass; JIT's process matrix uses the
rebuilt host. The aggregate AUTO baseline completed **7,542/7,543**. Its sole failure was
`latex_test_tex_script_digest` (status 124 at 60 seconds) in concurrently edited
LaTeX code outside this phase. Its focused default-deadline retry passed in
58.7 seconds. The aggregate is not recorded as green. A final JIT document
exchange (`82-sync`) concurrently exited by SIGKILL without a native diagnostic;
its isolated retry passed all 18 checks unchanged. Observed late-stage process-tree
peak RSS was about 5.2 GiB. Kernel logs do not establish a Lambda memory kill,
so resource contention remains unconfirmed. Neither deadlines nor goldens changed.

Evidence: `temp/sync-retention/final/{auto,interp,jit}-{sync,gc,retention,documents,peer,cloud}.log`,
rebuilt critical-case logs and `baseline.log`. Document links, golden pairing and
`git diff --check` pass. No native test harness, vendor source, owner source,
timeout or legacy expected result was changed to obtain these results.


| Final gate | AUTO | Interpreter | JIT |
|---|---:|---:|---:|
| Sync goldens | 63/63 | 63/63 | 63/63 |
| Focused every-allocation GC/poison | 6/6 | 6/6 | 6/6 |
| Retention process checks | 23/23 | 23/23 | 23/23 |
| Existing document process checks | 18/18 | 18/18 | 18/18 isolated |
| Existing peer process checks | 12/12 | 12/12 | 12/12 |
| Existing cloud process checks | 17/17 | 17/17 | 17/17 |

Reproduce with the commands in [the test map](../../test/README.md#sync-history-and-maintenance).
The final receipt retains both the concurrent failure and successful isolated
reruns. The backend was stopped only after confirming its exact owned command
and directory; its launcher reaped the child. No external repository was pruned.

# Repository recovery, scale and maintenance reuse

**Date:** 2026-11-12
**Status:** Complete for the bounded native profile; aggregate baseline residue recorded below.
**Starting commit:** `91e25b7a5` on `master`, main Lambda checkout.
**Design:** [Data sync §12.3](../Lambda_Design_Data_Sync.md).
**Authority:** S12.1.1v3 (pure policy/procedural IO), S12.4.4v4 (scope ownership),
D5.1.1–D5.1.3 (Input/GC ownership), D11.1.2 (native IO), D11.2.1–D11.2.4
(host grants), D9.3.4 (packages), D9.1.1v4 / D9.1.3v2 (project ownership).

## Current checkpoint

The authorized phase is implemented, primarily in Lambda Script. No new goal or
commit was requested. Preserve unrelated LaTeX changes and shared-ledger edits.
No vendor, lambda-input or Radiant source was changed. Native additions are
limited to streaming/hash mechanisms in Lambda `io_disk.cpp`, its registry and
header. The owned S3 backend and process-gate children have been stopped/reaped.

All planned implementation and qualification work is complete. The aggregate
baseline returned **7,573/7,574**; its only miss, `eps_fonts`, passed three
isolated retries with unchanged fixture/golden/deadline. Cause is unconfirmed;
this record does not claim a completely passing aggregate run. The final rebuilt
CLI passed all 70 interpreter/JIT sync goldens and all five GC/poison goldens on
AUTO/interpreter/JIT. Its SHA256 stayed constant through those post-build checks.
No required implementation step remains. No changes were committed.

Evidence is under `temp/sync-recovery-scale/`; checkpoint history is preserved in
`checkpoint-history.md`. The final scripts default new cloud spaces to catalog
v2. Existing controls/catalogs remain readable; explicit v1 test selection
continues to qualify compatibility.

## Completed milestones

1. **Paged metadata.** Immutable ordered pages contain up to 64 entries or 32
   children. Digests, ranges, counts and root-bound cursors are validated. V2
   catalogs track physical keys, reservations, receipt trees and retirement jobs
   by roots. Publications bind a service-validated dependency subtree rather
   than a growing receipt array. A protocol fixture publishes 8,193 object keys
   plus its bundle while keeping the catalog below 8 KiB.
2. **Audited recovery.** Bounded S3-compatible listing discovers catalog backup
   candidates without choosing a latest one. Owner recovery disables cleanup.
   Audit freezes admissions, scans allocations/backups/data, records ETags and
   quarantines unregistered bytes. The owner client authenticates conditional
   GET results, IDs, MACs and checkpoint proofs; it then re-reads every committed
   dependency and verifies the retained graph. A signed report binds the exact
   inventory, base ETag and heads before promotion. Missing, corrupt or changed
   data cannot yield a report. The audited cut copies retained data once into a
   fresh generation. Interrupted scanning and verification resume from durable
   cursors; a lost initial local save can adopt the frozen service job.
3. **Maintenance reuse.** Private encrypted locator pages map logical IDs to
   actual immutable physical keys. Normal cuts retain registered active copies,
   uploading only unpublished objects and a new bundle. Immutable deletion jobs
   bind old and keep roots; later publications cannot re-admit retired paths.
   Old jobs can reap delayed old PUTs after subsequent cuts without touching
   retained/new data. Cache pins bind exact paths across reused generations.
4. **Large files.** Native `disk.read_stream`, `disk.write_stream` and
   `crypto.hash_stream` preserve precise roots across script callbacks, validate
   source revisions, exact lengths and SHA256, and abort unpublished stages on
   error/cancellation. Scripts implement incremental Gear32, paged chunk
   manifests, authentication, installation and snapshot/saved-revision restore.
   New streamed `chunk_raw` objects use raw binary plaintext and a separate raw
   domain context; legacy kind numbers, contexts and small-file formats remain
   readable. All clients must support the new profile before using it.
5. **Qualification.** Exact goldens, forced-GC/poison, independent native clients,
   actual S3, interrupted cuts/deletes, legacy upgrade, offline return and release
   large-file capacity are covered below. The aggregate result and its isolated retry are recorded below;
   hosted/WAN/continuous-day capacity is not inferred from these runs.

## Safety findings and fixes

An aborted v2 cut initially reused its upload generation. The allocator now uses
a separate mutable high-water record plus create-only `allocations/<epoch>`
claims. Abort and pre-activation crashes consume their generations. Audit scans
claims, valid backups and data paths to choose a fresh generation. Losing both
mutable pointers still cannot overwrite an existing immutable claim: collision
forces replanning. `sync_reuse_fencing` and `sync_cloud_audit` cover abandoned
keys, stale receipts, delayed uploads/deletes and high-water loss.

The scheduled client previously mistook an aborted cut for a committed one and
tried to prune it. It now returns to selection. Independent process tests cover
the transition on all three tiers. No lease expiry cancels another writer.

Release investigation separated native streaming (~59.8 MB maximum RSS) and
script CDC (~76.8 MB) from a full workflow (~5.88 GB RSS / 10.34 GB footprint).
Typed scalar validation and text-encoded binary payloads repeatedly allocated
Input-owned values outside GC (D5.1.1–D5.1.3). Batching scalar portability checks
preserves canonical codec bytes; raw chunk plaintext and raw domain contexts
remove text parsing from the payload hot path. The old codec's captured SHA256,
type/order round trips, scalar-boundary injection and malformed types are checked
by `sync_codec_batch`. No generic Input lifetime or native/vendor GC change was
made. This identified excessive package allocation; it does **not** establish
the cause of the previous concurrent JIT document SIGKILL.

Initial server capture also consumed a connected peer's fixed deadline. The
agent now captures before advertising readiness. Subsequent scans reuse a file
reference only on exact path/type/identity/revision observations and revalidate
the named entry; changed tokens force capture. Connection deadlines are unchanged.

## Verification receipts

All focused checks use the final package algorithms. Native source was unchanged
after the release build. Logs and exact fixtures are retained in the evidence
root; normal/stress deadlines and existing expected outputs were not relaxed.

| Gate | AUTO | Interpreter | JIT |
|---|---:|---:|---:|
| Exact sync goldens (`final-goldens-*`) | 70/70 | 70/70 | 70/70 |
| Focused every-allocation GC/poison (`final-gc-*`) | 5/5 | 5/5 | 5/5 |
| Real-S3 recovery/scale (`final-process-*`) | 13 | 12 | 12 |
| Retention/cache/crash (`final-retention-*`) | 23/23 | 23/23 | 23/23 |
| Release 16 MiB local capture/edit/restores | pass | pass | pass |

The aggregate baseline (`baseline.log`) passed 7,573 of 7,574 tests, including
368/368 MIR forced-GC stress cases and all new sync fixtures. `eps_fonts`
(`test/lambda/eps/fonts.ls`) returned script status 1 in its batch, then passed
three isolated GTest retries (`eps-fonts-retry*.log`, 0.04-second script runs).
No timeout/golden change or unrelated EPS fix was made. The batch-only cause is
unconfirmed; it does not reproduce in the sync gates. The earlier LaTeX digest
baseline miss did not recur in this run. The full baseline took 18m19s.

Because the baseline rebuilt the CLI, `post-build-goldens-{jit,interp}.log` repeats
all 70 goldens and `post-build-gc-{auto,jit,interp}.log` repeats all five stress
cases. Every check passed, and `post-build-identity.txt` confirms the executable
was unchanged throughout. The full aggregate run was not repeated after the
isolated EPS retries.

The recovery gate transfers 16 MiB between independent peer processes, deletes
and recovers the catalog, rejects an object changed after discovery, completes
an audited cut, performs three cuts with zero retained-object uploads, replans
an aborted scheduled cut, merges returning offline work and verifies a fresh
cloud-only restore. Interpreter covers legacy-v1 upgrade. AUTO additionally
asserts the default bootstrap protocol, explaining its extra assertion.

The retention gate covers cache reservations/deferred hydration, unsaved offline
edits through pruned history, a lost committed-cut response, killed SQL/filesystem
prune work, writer fencing and scheduled maintenance. Focused GC covers stream
callbacks/cancellation, raw chunk repositories, audit metadata and retention.
Large optional `--full` every-allocation document workflows remain outside the
qualified budget; LR01-24 is still open.

Release capacity uses the native process's `/usr/bin/time -l` counters, without
changing ordinary deadlines. The 16 MiB workflow checks external replacement,
exact snapshot restore, exact saved-revision restore and repository verification.

| Release tier | Maximum RSS (bytes) | Peak footprint (bytes) |
|---|---:|---:|
| AUTO | 508,215,296 | 445,498,328 |
| Interpreter | 310,624,256 | 298,173,256 |
| JIT | 552,075,264 | 489,309,120 |

Other focused qualification was active; elapsed times are not isolated throughput
benchmarks. Early runs before the fixes and a sandbox-denied timing-counter run
are investigation evidence, not final capacity passes.

## Bounds and remaining qualification

Native streaming accepts up to 64 GiB; the script capture defaults are 256 MiB
per file and 1 GiB per scan. The measured large-file workload is **16 MiB**; the
native upper bound is not a capacity claim. Workspace, editor and individual
codec/physical envelopes remain 8 MiB. V2 publications support up to 65,536 object
paths; the large publication fixture qualifies protocol shape above the old
8,191-object cap, not full network throughput at that maximum. Local inventory,
prune and history limits remain explicit in the package guides.

Metadata epoch retirement, packing/compression, fine sharing/key rotation,
browser/GUI wiring, Internet traversal, other S3 dialects, hosted deployments,
Windows/Linux and continuous-day daemon capacity remain separate qualification.
Listing is discovery, not a globally transactional/latest-backup proof. Recovery
cannot reconstruct missing keys or already deleted sole copies. Unknown data
stays quarantined; no automatic admission or deletion is inferred from listing.
Namespace/CRDT identities and merge anchors remain retained.

## Identity and cleanup

The tested starting HEAD is `91e25b7a5`; package/native changes are uncommitted.
`source-identities.json` records every package/fixture/native source hash and
`native-identities.txt` records executable hashes. The release executable is
`temp/sync-recovery-scale/lambda-release.exe`, SHA256
`9a1ae3e48a36f08c2f5c3fb1cfade37828b5a73d60f50f1843ecb4fd2ce35124`.
The final debug executable SHA256 is
`4746cc9a430d10c6020904285452269cb8077691b89bd242240f6082b3e60b8b`.
Earlier real-S3 process gates used the pre-baseline debug executable, SHA256
`114e6283507d63513b80d4b23f1def9dcf546a4d5834680faf6d9b06a5351dab`,
with the same streaming native source. The final source/fixture identities include
uncommitted package changes; HEAD alone does not identify the tested implementation.

Backend PID **86485** was verified against its exact executable, owned store
directory and port **62258**, then received SIGTERM. Launcher **43960** exited 0;
port 62258 is closed. `backend-cleanup.json` records this. Process drivers join
all owned client/function children and close their fault proxies. Private storage
configuration remains in the protected evidence directory and is not committed.

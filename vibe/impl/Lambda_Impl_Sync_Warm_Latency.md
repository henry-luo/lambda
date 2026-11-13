# Warm LAN sync latency

**Date:** 2026-11-13
**Status:** Partial; wrapped up at the user's request. The subsecond goal remains open.
**Worktree:** Main checkout, `master`; started at `1118f5108`. Verification base:
`2585cffa5` plus this sync change set. Unrelated LaTeX work is untouched.
**Authority:** S12.1.1v3 (script policy), S13.1.2v2 / S13.3.1v2 (tasks),
D6.3 (precise roots), D9.3.4 (native IO), D11.1.2 (anchored IO).
**Design:** [Data sync §2.1.1](../Lambda_Design_Data_Sync.md#211-lan-change-detection-and-connection-bursts).

## Current checkpoint

- Goal: warm small-file arrival below one second. Measure completed installation
  separately; preserve authentication, durability, conflict handling and recovery.
- Implementation remains Lambda Script; no native, sibling or vendor changes.
  Ten quiet seconds still closes the connection. Prepared for commit at the
  user's request after wrap-up; the latency target remains open.
- Negotiated burst v2 sends complete missing atomic transaction sequences with
  full-space checkpoint-authority attestations. Original authors remain intact.
  Epoch/floor/size limits fall back to signed checkpoints; v1/finite remain.
- Small encrypted bundles combine objects and frames into one head transaction.
  Every packet is authenticated before publication; bad later packets roll back.
- Typed canonical validation fingerprints, immutable dependency proofs and derived
  projections reduce repeated work. Staging preserves the committed owner cache.
  Retirement/cache scope changes and maintenance invalidate dependency proof reuse.
  Explicit audits and installation still read/authenticate physical objects.
- Disk and managed commands have separate stable streams; document imports retain
  fresh actors. Capture applies atomic deltas and omits editor undo snapshots;
  saved file history remains. Workspace visibility avoids redundant ancestry work.
- Defaults are now 50 ms debounce and 250 ms maximum batch wait. Status exposes
  captured and fully installed frontiers; failure status retains the actual reason.
- Final correctness: **24/24 focused exact-golden runs passed**, eight fixtures on
  AUTO/interpreter/JIT. Temporary production tracing was removed before these runs.
  Evidence: `temp/sync-warm-latency/wrap-checks.log` and matching `.out/.err` files.
- Frozen release host: `temp/sync-lan-events/lambda-release-final.exe`, SHA-256
  `6241e7a8bc461958309a4933c9a4be78ce584ea5726e8b617cb9b490294e2e2b`.
  Final module/test identities are in `temp/sync-warm-latency/wrap-identity.json`.
- Earlier 10-file candidate: continuous visibility median 0.735 s, max 0.832 s;
  journal completion median 0.869 s, max 0.978 s. These measurements predate the
  final delta/capture changes and do not qualify the final source.
- Last completed 100-file candidate failed: continuous visibility median 3.892 s,
  max 4.184 s; journal completion median 5.299 s, max 5.517 s. Later changes have
  focused correctness coverage, but no replacement completed performance result.
- Broad sync sweep, aggregate baseline, final-source latency, three-peer scale,
  physical LAN devices and other operating systems remain unqualified this phase.
  Previous milestone baseline totals do not qualify this change set.
- Next action if resumed: measure the final source at 10/100 files, then isolate
  incremental validation, inventory enumeration and full-state persistence costs.
  Do not relax the timing threshold or durability to make the benchmark pass.

## Findings and evidence

Evidence paths below are relative to `temp/sync-warm-latency/`.

The old cold-versus-warm figures were not a controlled comparison: cold trials
preceded warm trials while repository history grew, and the warm trigger observed
file visibility before the previous installation/fan-out had fully settled.
This can overlap subsequent work. It does not establish connection reuse as the
cause of the slowdown.

Instrumented two-process reproduction with ten initial 4 KiB files measured
roughly 1.2–1.3 seconds. Repeated full-state validation and commits per checkpoint
frame, invalidation of the owner cache during staging, repeated content dependency
reads, and full historical capture joins consumed local CPU and storage time.
See `profile-before.log`, `trace-before.jsonl` and `stages-before.json`.

The optimized ten-file candidates passed the stricter completed-install gate:

| Candidate report | Continuous visibility median / max | Installation median / max |
|---|---:|---:|
| `acceptance-bundle10.json` | 0.680 / 0.823 s | 0.833 / 0.989 s |
| `acceptance-projection10.json` | 0.735 / 0.832 s | 0.869 / 0.978 s |
| `acceptance-final-100.json` | 3.892 / 4.184 s | 5.299 / 5.517 s |

These use independent processes on one macOS host, AUTO on the frozen release
host, 4 KiB edits and two peers. The ten-file runs have ten samples per mode;
the 100-file run has five. Report names describe historical candidates, including
the word `final`; none identifies the final source. Intermediate failures remain
in `acceptance-10.json`, `acceptance-memo10.json`, `acceptance-delta10.json` and
their logs. Preserve them rather than weakening the acceptance criterion.

An isolated 100-entry capture probe measured 432–512 ms per local edit before any
network or receiver work (`scan-probe100.log`, `trace-scan100.jsonl`). Within it,
capture/reconciliation consumed 165–199 ms, validation 177–235 ms and commit
59–74 ms. Canonical encoding accounts for 40–56 ms inside that commit; do not add
it again. This probe used short text values, not the 4 KiB delivery workload.
After proof memoization, dependency checking in this probe was about 3 ms.

Full historical event/receipt validation and serialization still scale with
repository state. Inventory enumeration also contains avoidable repeated linear
membership searches when selecting all stored objects, and repeated missing-page
requests rebuild inventories. These are remaining structural costs, not measured
stage attribution for a completed final-source delivery. They were not changed
following the user's wrap-up request.

The owner mailbox was checked and did not explain seconds of latency: the probe
measured about 0.553 ms for a full response versus 0.414 ms for a projection
(`rpc-probe.log`). The experimental projected-response API was reverted.
Temporary production telemetry was removed; preserved trace sources and logs
remain under this evidence directory.

## Verification and resumption

The eight final fixtures are `sync_warm_protocol`, `sync_peer_burst`,
`sync_workspace_moves`, `sync_workspace_lifecycle`, `sync_install_additions`,
`sync_document_races`, `sync_event_capture` and `sync_retention_gc`. The two
workspace fixtures are in `test/lambda/`; the others are in `test/lambda/proc/`.
Run with the frozen release host (`run` for procedural fixtures) and
`LAMBDA_EXEC_BACKEND=auto`, `interp` and `jit`, then compare exact output to each
`.txt` golden. The final wrap-up did so for all 24 combinations.

`sync_warm_protocol` covers object/frame batch commit, idempotency, tampered
checkpoint rollback, typed-cache rejection, complete multi-transaction delta
offers and rollback of a bad later signed transaction, separate disk/managed
streams, and repeated external document imports. `sync_peer_burst` explicitly
selects both v1 and v2 and exercises rounds in both directions.

`test/sync_connectivity/benchmark_warm.py` verifies reused sessions, exact bytes,
source capture and completed destination installation. It records settled and
continuous modes, workload, executable identity and distributions. Its default
gate requires every completed-install sample below one second; no threshold was
relaxed. Source hashes for older candidate reports were not recorded, so use
them as investigation evidence only.

To obtain new qualification after resuming:

```sh
LAMBDA_SYNC_EXE="$PWD/temp/sync-lan-events/lambda-release-final.exe" python3 test/sync_connectivity/benchmark_warm.py --seed-files 10 --samples 10 --report temp/sync-warm-latency/resumed-10.json
LAMBDA_SYNC_EXE="$PWD/temp/sync-lan-events/lambda-release-final.exe" python3 test/sync_connectivity/benchmark_warm.py --seed-files 100 --samples 10 --report temp/sync-warm-latency/resumed-100.json
```

Run timing without concurrent builds/tests and associate new reports with exact
source identities. Then run the full sync goldens, relevant native peer/LAN
process checks and the aggregate Lambda baseline before claiming broad readiness.
Physical-device LAN timing needs separate qualification.

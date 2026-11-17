# Multi-file sync performance investigation

**Date:** 2026-11-17  
**Status:** Complete for this tuning pass; remaining demo latency and memory limits below.  
**Authority:** S12.1.1v3, D11.1.2, D11.2.1–D11.2.4. Keep synchronization
policy in Lambda Script and retain authenticated durable publication.

## Current checkpoint

- Continue after [chunk profiles and packs](Lambda_Impl_Sync_Packs.md).
  Preserve all prior uncommitted changes. Use isolated data under `temp/`;
  do not modify the user's sync folder, profile or Wasabi repository.
- Existing release host SHA256:
  `d19fb8aa7d4cd04c68b314384fe30dc287e6050010301229e38d926313a56830`.
- Baseline local-S3 fresh publication: 8 MiB / 1 file 1.524 s;
  8 MiB / 128 files 4.943 s, including 3.029 s scan, 0.394 s pull,
  1.151 s publication, peak RSS 1765.7 MiB.
- Diagnose capture, CRDT reconciliation, validation, persistence and publication
  separately. Profile confirmed hot paths before changing implementation.
- Validation: release A/B on the same corpus, meaningful sync/codec regressions,
  byte-exact restore and interrupted upload as applicable. Record remaining limits.
- Diagnosis: fresh 128-file scan spends 0.644 s capturing 8 MiB and 1.650 s
  reconciling CRDT state. The same 128 files totaling 64 KiB spend 1.691 s in
  reconciliation, confirming a file-count cost independent of bulk transfer.
- Implemented linear exact packet budgeting, balanced pack byte concatenation,
  empty-scalar codec fast paths, reuse of measured capture group widths, removal
  of unused create-command projections, and combined control/catalog persistence.
- Intermediate release local-S3 result: 8 MiB / 128 files 3.552 s,
  890.4 MiB peak RSS. Isolated scan drops 3.072 -> 2.049 s and
  1784.6 -> 615.2 MiB RSS. These single samples are superseded by matched repeats.
- Owned isolated S3 supervisor ran separate master/volume/filer/S3 processes;
  all data, logs and sockets are under `temp/`. The supervisor and its children
  are stopped. Do not use embedded `weed server`, whose
  built-in gRPC socket paths target `/tmp`.
- Matched release A/B completed: three samples per arm/case, alternating arm
  order, identical source bytes, fresh repository each run, no concurrent tests.
  Report: `temp/wasabi-integration/multifile-ab-1794901808910904000/summary.json`.
  Before/tuned module snapshots and raw outputs remain alongside the report.
- Final full AUTO sync-related gate passes **114/114**; focused interpreter and
  JIT gates each pass **41/41**, including authenticated startup-cache coverage.
  Results and logs: `temp/wasabi-integration/multifile-checks/`.
- Full demo remains slower than the fresh publication harness: uninstrumented
  108-file runs take 5.940 and 6.035 s to back up, 7.337 and 7.232 s to restore.
  Do not present the 3.610 s fresh benchmark as the full demo result.
- Temporary opt-in demo instrumentation identified 2.555 s initial scan and
  1.609 s cloud synchronization in a 5.206 s instrumented backup. Diagnostics
  are removed and `lan_agent.ls` matches the saved pre-trace bytes exactly.
- A populated-repository startup probe found repeated cold head decoding and
  validation: resume 3.607 s, root status 1.805 s, initial scan 2.163 s.
  Carrying the existing authenticated cache changes these to 1.834 s, 0.000386 s,
  and 0.362 s. Single diagnostic probes, not matched end-to-end demo medians;
  no comparison of their cloud-sync stages is justified because the first probe
  may itself publish missing state.
- `open_repository`/`open_agent`/`resume_agent` accept optional caching, enabled
  by the LAN controller. `with_cache` shares the existing proof attachment.
  Head authentication and generation/ID binding remain mandatory on each access.
- Final interrupted upload check passes: pending on dropped transfer, retry,
  and byte-exact sender-offline restore. No Wasabi/user data modified.

## Findings and changes

1. `protocol.reply` serialized every growing operation prefix. Exact typed-JSON
   array sizes can be accumulated from independently encoded singleton entries,
   including comma delimiters and the final `more:false` overhead.
2. `pack.encode` appended each blob to the full accumulated binary buffer.
   Balanced concatenation reduces copied bytes from quadratic to logarithmic
   passes per blob while retaining byte layout, authenticated index and IDs.
3. `workspace_capture.apply_groups` repeatedly authored an oversized suffix,
   halving it to fit. Reuse the measured fitting width while checking every
   packet's actual bytes. Invalid zero widths now fail rather than loop.
4. Create commands projected history without consuming the result. Projection
   now occurs only for commands that consult existing entries.
5. Scalar-free codec encode/decode parsed an empty Mark document anyway.
   Skip that parse; retain portability and canonical checks for actual scalars.
6. Cloud pull committed the same full state twice for related control/catalog
   observations. Record both atomically, preserving rollback/fork rejection,
   and consume the returned state rather than loading it again.
7. Startup discarded the first validated loaded state, then decoded and validated
   it again for resume, root lookup and initial capture. Carry the existing proof
   until the repository owner takes over; a changed/forged head cannot reuse it.

## Validation and measurements

The same release native host is used for both arms; implementation changes are
entirely Lambda Script. Local SeaweedFS 4.53 runs separate loopback services.
All samples include initialization, scan and cloud publication. They exclude
restore. Peak RSS is macOS `/usr/bin/time -l`; MB/MiB labels remain distinct.

| Source data | Files | Before median | Tuned median | Before median peak RSS | Tuned median peak RSS |
|---|---:|---:|---:|---:|---:|
| 8 MiB | 1 | 1.535 s | 1.538 s | 441.6 MiB | 426.7 MiB |
| 64 KiB | 128 | 3.882 s | 2.771 s | 1150.3 MiB | 830.7 MiB |
| 8 MiB | 128 | 4.835 s | 3.610 s | 1939.7 MiB | 895.2 MiB |

### 80 MiB / 128-file attempt — 2026-11-17

A release run used 128 independently generated random files of 655,360 bytes
each (80 MiB total), a fresh repository and the same isolated local SeaweedFS
S3 publication harness. It failed during `local.scan`, before cloud pull or
publication, with `io.sqlite: statement failed or result exceeded its limits`.
The recorded wall time to failure was 1.17 s; this is not a sync-time result.
The run record is under `temp/wasabi-integration/measure-80m-128/`.

Root cause confirmed with a separately copied, instrumented sync package and
the same corpus: `pack_store.flush` queried all pending object records at once.
It requested 17,066,092 ciphertext bytes plus 3,091 ID and kind bytes across
76 rows, exceeding the SQLite result accumulator's 16,777,216-byte aggregate
limit. The flush threshold is 16 MiB and is checked after staging an object,
so the batch may already exceed the query limit. The 64 MiB create/clone image
limit does not apply to this existing database transaction. Diagnostic output:
`temp/sync-80-diagnosis/run2/err`. The error occurs before S3 (D11.1.2).

The subsequent [streaming-pack implementation](Lambda_Impl_Sync_Streaming_Packs.md)
removes SQL payload staging. The release 80 MiB / 128-file case now publishes
in 8.328 s with a 112 KiB metadata database and restores exactly in 13.530 s.
That report records the final samples and remaining memory/transport limits.

For 8 MiB / 128 files, median scan falls **2.936 -> 1.969 s**, pull falls
**0.396 -> 0.199 s**, and publication remains **1.131 -> 1.123 s**.
End-to-end publication improves 25.3%; peak RSS falls 53.9%. The unchanged
single-file control supports attribution to per-file overhead. Sample variation
means these observations are not latency guarantees or WAN/Wasabi qualification.

The remaining measured cost includes reconciliation, repeated canonical metadata
serialization/authentication, immutable full-state publication and metadata IO.
The known Input arena retention issue remains open (D5.2.5); balanced pack assembly
does not solve parser ownership. Roughly 895 MiB for 8 MiB of many-file input is
still excessive. Further tuning should break down the approximately 1.12 s
publication stage and the remaining reconciliation work before changing policy.

Cloud restore and interruption pass; final tier checks pass and the owned service
is stopped. New regressions cover exact byte budgets (including Unicode/escaping and
the final `more` flag), bounded capture groups, zero-width rejection, binary pack
assembly, atomic cloud observation rollback/fork rejection, external edits through
a cached handle, another writer advancing the head, and forged encrypted heads.

Final evidence:

- `multifile-checks/results.json`: AUTO 114/114, interpreter 41/41, JIT 41/41.
- `multifile-ab-1794901808910904000/{results,summary}.json`: matched release
  medians, raw stage timings and peak RSS; adjacent snapshots reproduce both arms.
- `multifile-demo.log`, `multifile-demo-cached.log`: uninstrumented full demo
  publication and byte-exact sender-offline restore across three data volumes.
- `multifile-demo-fault.log`: interrupted upload remains pending, retries, and
  restores all mixed-format files exactly with the sender offline.
- `startup-{before,cached}.log`: diagnostic startup stage breakdown; these are
  not end-to-end backup comparisons.
- `demo-trace/`, `multifile-demo-trace.log`: temporary instrumentation evidence;
  production script restored byte-for-byte afterward.
- Release native host SHA256 remains unchanged; no native rebuild was required.
  `git diff --check` passes. Changes remain uncommitted.

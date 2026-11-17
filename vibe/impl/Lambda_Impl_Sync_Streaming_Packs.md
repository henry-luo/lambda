# Streaming sync packs

**Date:** 2026-11-17
**Status:** Implemented and qualified locally; limits below.
**Authority:** S12.1.1v3, D11.1.2, D11.2.1–D11.2.4; design §5.9.

## Checkpoint

- Confirmed failure: pending-pack query returns 17,069,183 bytes through a
  16,777,216-byte SQLite result accumulator. No 64 MiB image limit is involved.
- Preserve the two pre-existing dirty investigation documents.
- Scoped native file spool mechanics and script pack/footer policy implemented.
- SQL ciphertext staging and duplicate object storage replaced with descriptors.
- Heads, credentials, migration, recovery and cloud pack reuse implemented.
- Negative/recovery checks and release 8/80 MiB publication/restore pass.
- All investigation files live under `temp/`; no user repository is a fixture.

## Implementation and evidence

- Script policy writes records into three scoped file spools (data, shared
  metadata, private state). SQL pending records contain descriptors only.
- The native spool supplies buffered append, range read, SHA256 and anchored
  immutable publication. Publication copies the scratch file through a 64 KiB
  buffer and verifies its digest, then resets the spool. Scratch files are
  unlinked while held so cancellation/process exit needs only close. No vendor/sibling source
  changes; the existing file-anchor API is reused.
- Heads and device credentials now have external immutable encrypted envelopes;
  SQL stores pointers. Legacy row payloads are checked against authenticated files
  before dropping columns; credential migration preserves validation failures.
- Cloud publication reuses eligible local packs; private/legacy mixed packs are
  filtered. The HTTP adapter still loads one bounded pack body, and capture still
  uses the serialized repository writer scope. Splitting capture from the writer
  transaction and file-backed HTTP upload remain performance improvements.
- Legacy SQLite free pages may retain old ciphertext. Logical migration does
  not claim physical compaction/secure erasure. New databases never store it.
- Historical external head/credential envelopes remain protected immutable
  recovery artifacts; reclaiming them needs coordinated retention. This pass
  does not turn those files into an automatically pruned history.
- Release build: `make build-release-compile`, `temp/sync-spool-build.log`.
- AUTO gate: 116/116 with local-service access. The initial sandboxed gate's
  nine local networking/monitor denials are environmental. Final focused
  interpreter and JIT gates each pass 24/24, including cancellation, escaped and
  foreign-task handles, migration, pending removal, rollback, corruption and
  recovery. Logs: `temp/sync-streaming-packs/checks-{interp,jit}.log` and
  `temp/sync-stream-gates-unrestricted.log`.
- The complete mixed-format demo survives an interrupted object upload,
  retains pending status, retries and restores every byte with the sender offline:
  `temp/sync-streaming-packs/demo-fault.log`.

### Release local-S3 measurements

Single samples, independent random 128-file trees, fresh repositories, same
publication harness as the earlier multi-file investigation. Each restore uses a
separate process after the sender exits, and every file matches SHA256.

| Data | Publication | Scan | Cloud publication stage | Restore | Publication peak RSS | Metadata DB |
|---|---:|---:|---:|---:|---:|---:|
| 8 MiB | 2.996 s | 1.569 s | 0.873 s | 6.734 s | 807.5 MiB | 84 KiB |
| 80 MiB | 8.328 s | 5.148 s | 2.614 s | 13.530 s | 1035.1 MiB | 112 KiB |

These are isolated local-S3 publication/restore observations, not demo or WAN
latency guarantees. The prior 3.610 s result was a three-run median; the new
single sample does not establish a matched speedup. Memory remains excessive.
Artifacts: `temp/sync-streaming-packs/results.json`, adjacent case directories.
Earlier implementation samples are preserved in `perf.log`; final release
samples are in `perf-final.log`. Final code additionally avoids repeating table
preparation on every non-migrating access. Changes remain uncommitted.
Final release SHA256:
`7bcdabf0e4d7706a111748fef15af5d5b884fe02902d5775ef87c3752db61a25`.
The final small gate passes 2/2; inspection of the 80 MiB fixture confirms no
payload column in any of its six tables and no pending pack descriptors.


### Follow-up memory attribution

The [release memory investigation](Lambda_Impl_Sync_Memory_Diagnosis.md) identifies
retained MIR compiler working state, unaccounted native binary GC pressure, Input
lifetime retention and resident VM pool capacity. The binary reproducer grows
from 31.7 to 289.8 MiB with zero collections. An 80 MiB publication retains 362.7
MiB more live native heap under AUTO than strict interpreter, predominantly MIR.
These are diagnostic comparisons, not an alternative sync mode or a runtime fix
(D5.3.1, D8.3.4). These observations describe the pre-tuning release host.


The closer follow-up identifies a separate formatter-result lifetime bug and
quantifies Mark validation churn, GC region capacity, generator/IR/context bytes,
and freed malloc residency. Both profiled 80 MiB restores match every SHA256.
The [memory tuning implementation](Lambda_Impl_Sync_Memory_Tuning.md#implementation-and-qualification--2026-11-17) covers these owners in independent phases and records clean release peak/idle and unchanged-round gates. P0–P5 and physical-pack download grouping are now implemented; the older measurements above remain historical streaming-pack results. See that record for matched final performance, remaining memory budgets and independent baseline failures.

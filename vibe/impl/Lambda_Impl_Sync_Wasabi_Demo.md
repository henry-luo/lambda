# Demo cloud backup and S3 qualification

**Date:** 2026-11-17  
**Status:** Local qualification complete; real Wasabi backup and authenticated readback passed.  
**Authority:** S12.1.1v3 (script policy), S12.4.4v4 and S13.1.2v2 (task
ownership), D11.1.2 and D11.2.1–D11.2.4 (host IO and authorization).

## Checkpoint

- User authorized connecting the demo to Wasabi and testing data under `temp/`.
  Storage credentials are in the existing user profile; do not print them or
  copy them into tracked files. The user gave bucket `lambda-sync`, region
  `ap-southeast-1`. The supplied cloud configuration is saved in the profile.
- Isolated local S3: SeaweedFS 4.53, release archive SHA-256
  `152a89d83e66372cc50c25d67e67a51ab295efbf85b88c139ca9965adc369b6f`;
  test-only service and credentials live under `temp/wasabi-integration/`.
- Existing native cloud acceptance passed against local S3, including lost commit
  response, killed client, authorization and retry. `existing-cloud.log`.
- A first mixed-file demo publication and cloud-only restore passed with the
  original device offline (49,364 bytes, 8 files); subsequent 1 MiB capture
  exposed `io.fetch` failure building response headers. A focused reproducer is
  `temp/wasabi-integration/http-after-scan.ls`. The diagnostic build confirmed
  that the shared shape tree declined header construction; its exact graph/fanout
  limit was not isolated. The release fix uses
  the private-shape fallback and the reproducer now passes.
- The demo invokes the stateless cloud handler locally, retaining LAN monitoring
  and repository ownership. Background backup uses an independent worker.
- The Lambda baseline passed 7,569/7,569 before the migration script changes;
  focused migration, pairing, cloud membership, UI and LAN tests passed afterward.
- The authorized real-folder backup uploaded 30 encrypted objects to Wasabi;
  all 30 were read back and authenticated. Cloud coverage was current, with
  no pending review or installation. Output: `temp/wasabi-integration/backup-user.out`.

## Local S3 qualification and measurements

The final release host `temp/wasabi-integration/lambda-release-fixed.exe` ran
on one Mac with an isolated SeaweedFS S3 service. Sender time includes startup,
scan and publication; restore time runs on an independent previously enrolled
receiver with the sender stopped. These are single samples and are not Wasabi,
WAN, sustained-load or tail-latency measurements (S12.1.1v3, D11.1.2).

| Total input | Files | Publish | Offline restore |
|---:|---:|---:|---:|
| 49,364 bytes | 8 | 1.982 s | 2.115 s |
| 1,065,172 bytes | 8 | 8.439 s | 6.420 s |
| 8,405,994 bytes | 108 | 171.791 s | 64.590 s |

Text, UTF-8, JSON, XML, Markdown, PNG, empty files and random binary were
compared byte-for-byte. A mid-upload disconnect left cloud coverage pending;
retry and offline restoration succeeded. Newly paired-device admission, LAN
regressions, authorization/revocation, stale CAS, dropped responses, client
restart, audit/corruption and retention-fencing cases passed. The existing 16 MiB
recovery gate restored exactly after catalog recovery and repeated maintenance.
Full logs are under `temp/wasabi-integration/`.

The 8 MiB run is a successful correctness test but revealed substantial resource
use. Earlier 8 MiB attempts were stopped at 9 GiB and 6.9 GiB footprints. The
last 8 MiB run's sampled intermediate footprint reached 5.1 GiB; its actual
peak was not recorded. The local locator probe for 2,208 objects improved from
14.211 s / 1,899,151,360 bytes RSS to 2.821 s / 368,082,944 bytes RSS after
reusing decoded pages. These individual observations do not establish the cause
of all remaining memory or runtime costs.

The original encrypted identity was version 1 with one version-1 paired device.
After explicit authorization, the current device was migrated to a new space;
the old agent and repository remain in place. The other existing device requires
re-pairing, but a relink flow for an already enrolled legacy profile remains to
be implemented. See [the crypto transition](../../lmd/package/sync/PEER.md#crypto-suite-transition).

Cloud settings are
currently plaintext in the user-supplied profile. Remote-function mode retains a
separate signed-URL logging issue recorded in the Lambda ledger; trusted local
mode sends directly signed S3 requests instead.

## Explicit legacy migration plan

The user authorized replacing this device's version-1 identity and re-pairing
its one legacy peer later. Preserve the old account directory and keychain entry
without mutation. Stage a new encrypted agent in a separate account-scoped
subdirectory, generate a fresh P-256 identity, secret and distinct v2 space, and
capture the existing working folder. Only after successful capture should an
atomic profile update select the new agent/space. If staging fails, the old
profile remains selected. Old history remains in the original repository; it is
not represented as new cloud history. This follows the existing crypto-transition
boundary in D11.2.3–D11.2.4. New join invitations must bind the same user and
accept the owner's authenticated fresh v2 space; legacy devices still require
explicit migration/re-pairing. Test staged failure, archived-data preservation,
restarts, fresh peer enrollment and cloud restore before using the real profile.

## File count versus byte count investigation — 2026-11-17

Authority: S12.1.1v3, D11.1.2, D5.2.5. Fresh independent spaces,
incompressible random bytes, release host, local S3, no competing benchmark.
The stage runner uses the demo's repository-owner cache. Measurements include
initial capture and publication, not receiver installation. Single observations;
they are not directly comparable to the earlier cumulative demo workload.

| Bytes | Files | Before: total | After batch index update: total | After: capture | After: publish | After: peak RSS |
|---:|---:|---:|---:|---:|---:|---:|
| 65,536 | 1 | 1.054 s | 0.778 s | 0.200 s | 0.163 s | 387 MiB |
| 65,536 | 128 | 11.329 s | 8.173 s | 5.081 s | 2.302 s | 1,059 MiB |
| 1,048,576 | 1 | 7.087 s | 4.510 s | 2.716 s | 1.463 s | 518 MiB |
| 1,048,576 | 16 | 8.766 s | 5.646 s | 3.312 s | 1.929 s | 732 MiB |
| 8,388,608 | 1 | failed | 35.858 s | 21.925 s | 13.551 s | 1,561 MiB |
| 8,388,608 | 128 | not measured | 51.285 s | 28.490 s | 21.781 s | 3,083 MiB |

Both file count and byte count matter. A diagnostic runner without the owner
cache took 55.121 s for 128 files / 64 KiB; do not present that as the running
demo's latency. The original large baseline twice failed with a connection error
about 19 seconds into publication while the S3 listener remained alive; its
completion time is unknown. This failure is unresolved, not a successful timing.

`metadata_pages.upsert_many` previously called `upsert` for every key, repeatedly
reading, parsing, hashing and writing the same root/leaf paths. It now routes
updates by page, preserves untouched subtrees and no-op roots, and keeps the
last duplicate value. Multi-level, boundary, duplicate, unchanged, old-root and
invalid-key checks pass across auto/interpreter/JIT. The local S3 cloud protocol
fixture retains all 16 passing checks.

`codec.decode` decodes sync records, not original PDF/image/document contents.
It preflights bytes, parses typed JSON, parses scalar Mark literals, reconstructs
values, and re-encodes to enforce canonical form. Metadata page reads additionally
encode again to check the page hash. Small chunk payloads and cloud ciphertext
envelopes still cross this text codec; streamed raw chunks bypass it locally.
The 4 KiB target produces roughly 2,000 objects for 8 MiB random input.

The general parser allocates through InputManager's retained per-thread pool.
A minimal process parsing the same 78,892-byte JSON array once versus 100 times
used 29,212,672 versus 52,822,016 bytes peak RSS. This proves a retained-allocation
path but does not attribute the entire sync footprint to that path. The source
trace and memory observation are recorded in the runtime issue ledger. Neither
the remaining capture cost nor the full memory amplification has been isolated
to a single function. Next profiling targets are per-byte script chunking,
repeated codec validation, full state serialization and binary transport envelopes.
The seconds-level 8 MiB target remains unmet.

Evidence and resumable runners: `temp/wasabi-integration/perf_matrix.py`,
`_perf_cached.ls`, `perf-cached-final.log`, `perf-cached-large.log`,
`perf-bulk/results.json`, `_parse_retention.ls`, `parse-{1,100}.err`.

## Local capture breakdown — 2026-11-17

Authority: D11.1.2, D5.2.5. Same 8 MiB random file, fresh repository for
each probe, release host, no network. Timings below are cumulative alternative
operations, not additive phases from one trace.

| Operation | Seconds |
|---|---:|
| Stream read and SHA-256 | 0.035 |
| Stream read, hash and Gear32 chunking | 0.348 |
| Create encrypted chunks, pages and manifest | 20.352 |
| Full local scan including workspace reconciliation/commit | 21.157 |

The file yields 1,720 chunks. Each new object is individually installed through
`io.disk.install`: `disk_open` selects `FILE_ANCHOR_FULL_SYNC`;
`file_anchor_write_commit` calls sync on both the staged file and the parent
directory after publication. macOS sync performs `fsync` then `F_FULLFSYNC`.
Chunk objects alone therefore request 3,440 full-sync barriers, plus the page,
manifest and state objects. Repository persistence also rereads and authenticates
each new object and queries/inserts its SQLite locator. A 3-second persistence
sample contains 2,500 main-thread samples: about 1,733 are in `fcntl` beneath
the write path, with further `fsync` and filesystem calls. This supports durable
per-object writes as the dominant measured local capture cost. The exact total
time attributable to each syscall has not been instrumented.

Correction to the earlier profiling hypothesis: the script's per-byte chunker
is not the main cause of the 21.92-second capture. Chunking plus read/hash is
under 0.35 seconds here. Creating the local objects adds roughly 20 seconds;
workspace work adds about 0.8 seconds in these independent runs. The content-only
process peaks at 171,704,320 bytes RSS, and full scan at 262,799,360 bytes;
the later multi-GiB peak must not be assigned wholly to local capture.

Next implementation candidate: immutable pack files with indexed chunk offsets,
bounded groups and durable publication before head/index references become
visible. Preserve crash safety; do not remove sync barriers from the generic
atomic-write API. Account for interrupted staging, replay, deduplication,
compaction and authenticated recovery before changing the storage layout.
Evidence: `temp/wasabi-integration/capture_probe.py`, `_capture_probe.ls`,
`capture-breakdown/results.json`, `capture-persistence.sample`.

## Pack refactor follow-up

The chunk profile and pack implementation supersede the earlier per-object
persistence path. See [pack implementation and final qualification](Lambda_Impl_Sync_Packs.md).
The same 8.41 MB / 108-file demo now backs up in 6.273 seconds and restores in
7.375 seconds against local S3. These are single release samples; many-file memory
remains high and the original Wasabi backup is not rewritten by these tests.

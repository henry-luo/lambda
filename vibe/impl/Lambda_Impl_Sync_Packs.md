# Sync chunk profiles and immutable packs

**Date:** 2026-11-17  
**Status:** Complete for the requested chunk profile, packing and normalized chunking; limits below remain.  
**Authority:** S12.1.1v3, D11.1.2, D11.2.1–D11.2.4; repository working
design: `lmd/package/sync/LOCAL_REPOSITORY.md`.

## Current checkpoint

- User requested 512 KiB minimum, roughly 1 MiB average, 8 MiB maximum chunks,
  encrypted blob packs and applicable FastCDC optimizations.
- Main checkout has prior uncommitted Wasabi integration, migration and metadata
  batching work. Preserve it. No production repository or Wasabi migration is
  authorized by this implementation task; qualification uses isolated temp data.
- Baseline release capture: 8 MiB, 1,720 old chunks, 20.35 seconds persisting
  content versus 0.348 seconds chunking. Per-object full-sync barriers dominate.
- Implemented a versioned normalized Gear profile; retained legacy Gear32 decoding
  and boundaries. Store new profiles in manifests; old repository profiles remain
  readable, and default-profile upgrades apply only to future captures.
- Immutable encrypted pack framing must support authenticated index recovery,
  scoped IDs, deduplication, mixed legacy loose objects, and durable pack-before-
  locator/head publication. Pack storage also requires retention/repack, cache
  removal, index rebuild, corruption and interrupted-write tests.
- Keep policy/codecs in Lambda Script. Native changes are bounded disk ranges
  and streaming block limits needed for 8 MiB records. Do not edit dependencies.
- Cloud packs require signed logical-ID to physical-pack mapping; update audit,
  recovery, maintenance and locator validation together. Legacy envelopes remain
  readable. Verify large single chunks at every transport limit.
- Implemented chunking, local packs, binary transfer, v5 cloud mapping and audit/
  cache integration. Batched local pruning defers predecessor deletion to a later
  scope. Added maximum-size, recovery, corruption, normalization and profile-upgrade
  tests. Existing default profiles upgrade on open; unchanged files retain refs.
- Release results: same 8 MiB content-only capture 20.352 -> 0.726 s; full capture
  21.157 -> 0.824 s (7 chunks). Fresh one-file 8 MiB publish pipeline 1.394 s;
  128-file 8 MiB 4.843 s. Peak RSS 446 MiB and 1,892 MiB respectively: many-file
  memory remains excessive and is not fixed by packs alone.
- Demo: 8.41 MB / 108 files backup 6.135 s, sender-offline restore 7.669 s;
  interruption/retry test passes. 16 MiB catalog recovery/audit/maintenance and
  offline-only restore acceptance passed. Earlier 65-fixture release sweep passed;
  later pruning/profile-upgrade changes are covered by the final tier sweep.
- Final baseline: 7,575/7,576 initially; the only failure was the old chunk vector
  inheriting the new default. It now explicitly selects the legacy profile.
  The complete main script baseline rerun passes 2,450/2,450. The other 39
  baseline executables passed in the initial aggregate. A broader unfiltered
  LaTeX-inclusive rerun was stopped; it is not counted as qualification.
- 36 focused release checks pass across AUTO/interpreter/JIT. Shared-pack cache
  eviction, maintenance and hydration pass; the server now preserves positional
  ID/key mappings and duplicate physical pack keys.
- Final release host restored, SHA256
  `d19fb8aa7d4cd04c68b314384fe30dc287e6050010301229e38d926313a56830`.
  Final matrix is `temp/wasabi-integration/perf-packed-final/results.json`.
  Final demo: backup 6.273 s and sender-offline restore 7.375 s for 8.41 MB /
  108 files. All comparisons are byte-exact. No real Wasabi or user repository
  was modified during this refactor. The owned local test service has been stopped.
- Completion: no required implementation remains. Known limitations are the
  many-file memory footprint, whole-pack retention during ordinary cloud cuts,
  and wire updates required on every participating device. Packing is compatible
  with legacy reads; it does not make Lambda a restic-compatible repository.

## Validation gates

Chunk bounds, normalization distribution, streaming parity, insert/delete reuse,
legacy compatibility; packed read/import/export/dedup; interrupted publication,
corrupt index/payload, locator reconstruction; retention of shared live blobs,
offline cache and history restore; local S3 publication/restore/audit/recovery;
LAN/P2P large-chunk limits; release file-size/file-count matrix and peak memory.

## Final release measurements

Same local S3 service, fresh independent repository per matrix case, no competing
benchmark, single samples. Times include initialization, capture and publication;
RSS is the process maximum reported by macOS `/usr/bin/time -l`. These are local
measurements, not Wasabi/WAN qualification (D11.1.2).

| Total bytes | Files | Publish pipeline | Peak RSS |
|---:|---:|---:|---:|
| 65,536 | 1 | 0.487 s | 253.4 MiB |
| 65,536 | 128 | 4.001 s | 1028.4 MiB |
| 1,048,576 | 1 | 0.772 s | 377.3 MiB |
| 1,048,576 | 16 | 0.912 s | 456.2 MiB |
| 8,388,608 | 1 | 1.524 s | 440.9 MiB |
| 8,388,608 | 128 | 4.943 s | 1765.7 MiB |

The cumulative demo workload uses the same 8.41 MB / 108-file scenario as the
earlier 171.791 s backup / 64.590 s restore observation. It now takes 6.273 s /
7.375 s. Fresh one-file 8 MiB publication takes 1.524 s, including 0.825 s
capture. The many-file RSS peak remains high; do not call this production-ready
on the strength of throughput alone. Whole-pack cloud retention is documented in
`lmd/package/sync/MAINTENANCE.md`; the fresh-generation replacement can reclaim
dead members from otherwise live packs.

The [subsequent multi-file investigation](Lambda_Impl_Sync_Multifile_Perf.md)
supersedes these performance observations for the latest scripts. Matched release
repeats reduce fresh 8 MiB / 128-file publication from 4.835 to 3.610 s median,
with median peak RSS 1939.7 -> 895.2 MiB. The repository and wire formats stay
unchanged; the remaining memory footprint is still excessive (D11.1.2, D5.2.5).

## Verification evidence

- `packs-baseline.log`: other native baseline suites pass; initial main script
  fixture failure was the old boundary vector using the new default.
- `packs-script-baseline-final.log`: main baseline 2,450/2,450 passes after pinning
  the old vector to `legacy_profile`. The broader LaTeX-inclusive diagnostic run
  was stopped, and is excluded from validation claims.
- `pack-tiers/results.json`: 36 focused AUTO/interpreter/JIT checks pass, including
  a single 8 MiB chunk, pack corruption, unindexed durable pack recovery, pruning,
  legacy references, external-event capture and binary peer exchange.
- `packs-scale-final.log`: 16 MiB peer install, catalog recovery, audited rewrite,
  corruption rejection, repeated cuts, offline return and cloud-only restore pass.
- `packs-demo-fault-final.log`: interrupted upload stays pending, retries and
  restores exactly with the original sender offline.
- `packs-cache.log`: shared-pack cache eviction, normal maintenance cut and
  authenticated hydration pass. `sync_pack_pins` separately pins positional
  mappings with repeated physical keys.
- `packs-demo-release-final.log`, `perf-packed-final/results.json`: final release
  timings and exact restores. `git diff --check` passes.

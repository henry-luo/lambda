# Sync test latency — diagnosis and optimization plan

**Date:** 2026-11-17  
**Status:** Implemented and qualified; independent baseline failures remain recorded.  
**Tree:** `ae76dbf6d` plus the uncommitted implementation; initial diagnosis used `e1b1ac324`.  
**Authority:** D5.1.3, D5.2.4, D5.3.1 (ownership and measurement),
D6.4.3 (context-owned module values), D8.3.1–D8.3.4 (AUTO and promotion),
D11.1.2 (IO boundary), S12.1.1v3 (procedures), S13.3.1v2 (task lifetime).

## Completion checkpoint

- User-authorized implementation complete, 2026-11-17, on `ae76dbf6d` plus the
  uncommitted changes. No commit requested. No diagnostic process/service remains.
- Burst close ordering, scoped fixture batching with independent commit/replay,
  canonical byte/digest reuse, document reply reuse, bounded current-head view
  reuse and type-exact settled no-op handling are implemented. All wire,
  authentication, durability and save recovery boundaries remain.
- Shared owner fixtures retain explicit cold reopens; a separate full cold
  fast-peer case preserves uncached protocol negatives/duplicates. New fixture
  scope and cache invalidation/corruption/budget regressions pass.
- All 117 sync goldens pass under normal fixture settings. Twelve strict
  interpreter and twelve eager-JIT cases pass. Forced precise GC/poison passes.
  Local-S3 protocol recovery and dropped-upload/exact restore gates pass.
- The modern/legacy compiler timing reader passes 2 native regressions and a
  5-case actual batch capture. Lambda's duplicate Input resolver is removed;
  fresh release and all 201 Radiant document cases pass. No sibling source edit.
- Full Lambda baseline: 7599/7604 on its first run. Three environmental S3
  failures pass with the endpoint unset in release and actual debug batch.
  Two constrained-error failures remain in the unchanged shared core, Input
  LI-076. The previous preserved release reproduces the golden failure.
- Radiant baseline: 8170 pass, 297 partial, 20 skip, one outdoor-import E228
  failure. Isolated renderer/import probes reproduce it. Prior release accepts
  the import; default-argument effect diagnosis remains separate from sync.
- Matched normal release: 42/42 exact outputs across 3 runs per arm/test, one
  frozen executable and verified HEAD library/test overlays. Serial median sum
  falls from 59.443 to 20.086 s. Timing/RSS ranges and caveats follow below.
- Frozen executable SHA256:
  `2c072951de32251f6b68ea0dfa89547e00552f2217f337ef12df18b27ffb7020`.
  The later native source change is a resolver ownership comment only.
- Evidence/runners: `temp/sync-test-performance-impl/`. Original diagnosis remains
  under `temp/sync-test-latency/`. Fixed issues are archived in Lambda's ledger;
  Input's only change is the LI-076 issue record.

## Measurement identity and limits

The reported 6–20 s figures came from the full debug baseline's parallel batch
workers. These release measurements isolate the scripts serially, including
process startup/teardown, using the preserved normal release executable SHA256
`130e5cb53f176d02d5ac1d22c752118a90a08062fa9a22bf1362aced8565f9d6` from the
memory-tuning completion. Each run uses fresh PID-scoped repositories. Profiled
runs are separate from timing qualification. No test timeout, golden, fixture
size or protocol coverage was reduced.

| Original test | Release median | Range, three runs | CPU seconds, median across all threads | Full-sync calls / elapsed in a separate diagnostic run |
|---|---:|---:|---:|---:|
| `proc_sync_peer_burst` | 19.859 s | 19.814–20.736 s | 2.637 | 639 / 2.374 s |
| `proc_sync_document_repository` | 7.590 s | 7.368–18.433 s | 7.885 | 282 / 1.008 s |
| `proc_sync_metadata_batch` | 7.528 s | 7.493–8.301 s | 2.693 | 1325 / 5.058 s |
| `proc_sync_peer_fast` | 6.628 s | 6.416–12.204 s | 6.915 | 220 / 0.792 s |
| `proc_sync_paged_publication` | 7.202 s | 7.144–8.254 s | 2.901 | 1249 / 4.760 s |
| `proc_sync_retention_repository` | 6.005 s | 5.958–12.820 s | 5.612 | 434 / 1.576 s |
| `proc_sync_warm_protocol` | 5.576 s | 5.474–9.278 s | 5.980 | 243 / 0.857 s |

CPU time includes background compiler/IO threads and can exceed wall time.
Barrier time sums individual calls, including possible overlap across threads;
it cannot be added to CPU time to reconstruct wall time. The first sweep's
large outliers are retained. Their complete variance is not attributed here;
AUTO publication timing and OS warming require further controlled comparison.

The macOS diagnostic interposer executes the real `fsync` and `F_FULLFSYNC`
syscalls and records durations. All seven instrumented outputs match goldens;
no unexpected `fcntl` command or barrier failure occurs. Ordinary `fsync` totals
are only a few milliseconds; the expensive calls are `F_FULLFSYNC`. Directory,
journal and object-file barriers share this table. The diagnostic's broad
`database` path category also catches files under `.sqlite.objects`; do not use
that category to infer SQLite transaction counts for repository tests.

Only the burst test opens loopback WebSockets. The other cases exercise local
repository/SQLite operations and direct protocol functions; the cloud fixture is
a SQLite callback adapter. Their elapsed time is a complete workflow, including
negative cases and recovery, rather than a measurement of one file's LAN arrival.

## 1. Burst: three avoidable five-second close waits

The test runs protocol v1, v2 and v3. After each client task finishes, it does:

```
let sent = wait(client)^
let elapsed = clock()-started
let served = wait(server)^
...
ws.close(socket)^; ws.close(listener)^
```

`peer_burst.accept` finishes its server exchange by calling `ws.close(socket)`.
The procedural WebSocket close waits for the peer's Close frame;
`lambda/runtime/io_websocket.cpp` sets `WS_CLOSE_TIMEOUT_MS = 5000`. The test's
client socket stays open while its main task waits for that server task.

High-resolution stage markers show **4.999–5.000 s** between client completion
and server-task completion for every protocol. Total unintended waiting is
about 15 s. The callback-triggered scan/write is about 0.13–0.19 s in the
instrumented run. The test intentionally keeps `idle_ms:500` and asserts at least
two rounds; those requirements remain.

A temporary copy closes the client socket immediately after `wait(client)`,
before waiting for the server, and removes the later duplicate close. Three
runs take **4.862, 4.928 and 4.880 s**, with identical golden output. Production
`peer_burst.connect` already closes its client socket when its exchange returns;
the test's ordering is the identified cause of this particular 15 s penalty.

**Implementation:** apply this ordering to the original test, retain all three
protocols, the 500 ms idle-close assertion and callback/owner-mailbox checks.
Add a generous focused test budget that catches the three timeout waits without
asserting a fragile subsecond total. Do not shorten the generic WebSocket close
timeout to hide the test's ordering.

## 2. Metadata tests: fixture transaction amplification

`test/lambda/proc/mod_sync_cloud_store.ls` opens a new file-backed SQLite
transaction in each `get`, `head`, `write` and `erase` callback. Each immutable
page/receipt `write` therefore commits independently. The native SQLite scope
requests `synchronous=EXTRA`, `fullfsync=ON` and checkpoint full sync.

- **Batch metadata:** 2,101 entries; 84 batch updates; an independent sequential
  upsert oracle; no-op, empty, duplicate-key, fresh-root and invalid-key cases.
  The sequential reference loop takes about **5.3 s**, versus **1.2 s** for the
  batch update, in the coarse stage run. The barrier trace contains **882 journal
  plus 441 database full syncs**, and two creation barriers. It spends 5.058 s
  in full syncs out of 7.280 s elapsed in that diagnostic run.
- **Paged publication:** 8,194 dependency keys and 129 staged receipt groups,
  followed by receipt merges, publication/replay, stale generation rejection
  and full traversal. Initial page-tree construction takes about **2.4 s**;
  staging child receipts takes about **3.7 s**. Its 1,249 full syncs consume
  4.760 s out of 7.419 s in the diagnostic run. The 8,194-row seed already uses
  one transaction; repeatedly committing generated metadata dominates here.

A temporary transaction-scoped fixture reuses the original read/write callback
logic inside a shared transaction. With all original assertions retained:

| Experiment | Three runs | Median |
|---|---|---:|
| Batch metadata | 1.149 / 1.151 / 1.154 s | **1.151 s** |
| Paged publication | 1.301 / 1.293 / 1.310 s | **1.301 s** |

These demonstrate removable fixture overhead. The paged experiment shares a
transaction across stages that production executes as separate requests; it is
not a replacement for request/crash-boundary qualification.

**Implementation:** add an explicit scoped fixture API using shared read/write
helpers. Batch pure page construction and the sequential algorithm oracle.
For paged publication, define transaction scopes deliberately: bulk setup and
algorithm checks may share a scope; commit/replay and request-failure acceptance
must retain independent committed scopes. Preserve the real SQLite/S3 recovery,
lost-response, negative CAS and interrupted-upload gates. Keep the 8,194-key
boundary case. Do not disable durability in the general SQLite API or reduce
fixture cardinality to obtain a fast test.

## 3. Repository cases: repeated state decoding and validation

These cases use small payloads but perform substantial repository work:

| Test | Work responsible for its elapsed time |
|---|---|
| Document repository | open/reopen, unsaved edit recovery, save preparation/install/finalization, external merge, structured save, old-version restore and verification |
| Fast peer | initial 20-file replication/materialization, forged acknowledgment rejection, valid/duplicate delta, and a 70,000-byte fast-path decline case |
| Retention repository | three saves, snapshot protection, paging, pin/stale-plan checks, forgetting, resumable pruning, version and snapshot restore |
| Warm protocol | initial 20-file transfer, corrupt/duplicate bundles, atomic deltas, two directory transactions and repeated external-document refresh |

The document stage trace spends about **1.3 s** in structured save and **1.7 s**
in the final restored-version save. Fast peer spends about **2.0 s** on initial
scan/trusted transfer/materialization. Warm protocol's final open/refresh
operations take roughly **0.6–0.8 s each**. These are full API workflows.

The low-level handles in these tests generally omit the persistent agent's
repository owner/cache. `local.status` and `local.mutate` consequently load,
decode and validate full state again. Document reads also call
`documents.expand` → `history.expand`: retained versions are loaded, validated,
restored/projected and checked again. `documents.flush` visits every open record,
including histories and dependencies, during mutations.

The independent compiler phase TSV reports about **76 ms** of frontend work for
the document case and **19 ms** for batch metadata. This does not include all
hot satellite compilation. Native samples from a release relink retaining symbols
show execution/dynamic dispatch, map lookup/shape construction, collection,
owned parse copying and formatting, plus background MIR compilation. They do
not support assigning the CPU-heavy cases primarily to import parsing. The
symbol relink has a different text hash from the preserved timing executable;
it is diagnostic corroboration, not a new normal-release timing arm.

Existing MIR helper counters expose the repeated work directly:

| Profiled original | Codec `encode` entries | `fn_parse2` calls | `fn_format2` calls | Equality helper calls |
|---|---:|---:|---:|---:|
| Document repository | 14,843 | 10,014 | 538,783 | 20,700,772 |
| Fast peer | 6,431 | 6,101 | 392,756 | 13,103,346 |
| Warm protocol | 10,056 | 9,315 | 426,564 | 9,039,562 |

These are counts from separate instrumented executions, covering instrumented
MIR calls, not a complete count of T0 calls or per-function timing. Promotion
publication affects the exact counts. The document profile also records about
1.11 million visits each to recursive scalar validation and tuple packing, plus
10.61 million array pushes. Even tiny documents cause large repeated traversals.

A temporary fast-peer copy uses the **existing** `lan_agent.run` pattern: enable
the cache, initialize its validation proof, and start one repository owner per
handle in the long-lived main scope. No protocol, key, file or assertion changes.
It completes in **1.494 / 1.532 / 1.498 s**, median **1.498 s**, versus 6.628 s.
This demonstrates the impact of repeated uncached state work; production LAN
agents already use that owner/cache pattern. Preserve cold/reopen coverage when
adding representative warm-owner tests.

**Implementation priorities:**

1. Reuse the existing owner/cache in tests intended to model long-lived agents;
   keep explicit uncached reopen, recovery and physical-corruption cases.
2. Return status from already loaded/committed state inside document operations
   where no independent observation is required. Avoid a second full
   `local.status` immediately after a transaction solely to shape its reply.
3. `local.mutate` currently performs document flush and validation before its
   no-change return. Add a proven no-op path where operation/maintenance
   semantics permit it, retaining staged-object finalization and required
   transaction effects. Keep history hydration out of ordinary status: reuse authenticated immutable
   headers/versions and page versions when requested. Cache expanded/validated
   document state by immutable identity and the relevant maintenance/cache epoch.
   Bound cache bytes, root values precisely and bypass reuse for explicit verify.
   Key caches by repository/key context and expected kind; preserve fresh-head
   authentication and external-edit/retention/revocation invalidation.
4. Serialize once per immutable value and reuse the canonical bytes/digest.
   For example, metadata page reads currently canonical-decode, re-encode inside
   decode, then encode again to check the page digest. After canonical validation,
   hash the already checked bytes using the same digest contract. Preserve wire
   spelling, ordered map keys, numeric types and all negative canonical tests
   (S2.3.1v2, S5.2.1v2).
5. Only then profile native/runtime costs left in repeated map construction,
   array building and AUTO promotion. Avoid moving sync policy wholesale to C++
   or using a global unbounded cache to hide repeated work.

Document save's preparation, install intent and finalization commits are recovery
boundaries. They must remain separately durable. An optimization must reduce
redundant observation/validation around those boundaries, rather than combine
all save stages into one transaction.

## Recommended order and acceptance gates

1. Fix the burst test's close order. Expect removal of approximately 15 s while
   preserving the same assertions and all protocol variants.
2. Introduce explicitly scoped metadata fixtures; qualify algorithm and durable
   request tests separately. The 1.15/1.30 s experiments are useful bounds, not
   promises for a request-accurate acceptance test.
3. Add representative cached-owner coverage beside required cold/reopen checks;
   implement status/hydration reuse in the document APIs. Use helper counts,
   allocations and individual operation latency as regression evidence.
4. Re-run the seven scripts serially three times in normal release, then the
   focused baseline gate. Keep original outputs, negative cases and recovery
   checks. Maintain memory plateaus from the memory-tuning qualification.

Do not relax full-sync settings, authentication/canonical checks, fixture sizes
or execution-tier coverage as a substitute for these changes.

## Diagnostic telemetry issue

The existing batch timing reader expects `mir_lower_us` immediately after
`index_us`, while current runtime records contain `analysis_us` between them.
Its `sscanf` matches six fields rather than the required ten, so the helper
rejects the record. The per-script wall figures remain valid. Recorded in the
[central ledger](../Lambda_Issue_Ledger.md#batch-compiler-timing-analysis-field)
under D5.2.4; the independent phase TSV and native samples were used here.


## Implementation — 2026-11-17

Policy remains Lambda Script (S12.1.1v3). No wire version, S3 schema,
authentication/canonical check or full-sync setting changes.

- Burst closes the client's WebSocket before joining the server task. All three
  protocols and existing round/idle assertions remain.
- The test store has one shared `access` helper and an explicit `scoped` callback.
  Metadata construction/oracle work can share a SQL transaction. Paged setup
  commits before publication and replay run through the independently committed
  adapter. New regression checks CAS, paging, rollback and adapter expiry.
- `cloud_schema.encoded_digest` hashes canonical bytes already produced or checked
  by the codec. Page persistence encodes once; page reads hash their checked
  canonical body instead of encoding the decoded tree again.
- Document open/edit/refresh/compact/cancel/final-save replies use their already
  observed/committed state. Save intent, physical installation and finalization
  keep their separate durable boundaries.
- The existing authenticated current-head cache can retain its expanded view up
  to **1 MiB of canonical bytes**. Larger views fall back to hydration; this adds
  no multi-head dictionary or historical cache. The existing validated-state
  proof/cache remains. Reuse is gated by fresh authenticated head generation and
  immutable ID. Explicit verification hydrates and reads physical objects afresh.
- Only a head settled by a completed mutation can take the no-op path; a
  type/ordered-key-exact comparison proves the callback returned the same state.
  Pending maintenance/cache jobs disable it. Outer SQL and pack finalization
  still run, so an operation's staged objects are not silently discarded.
- Shared test owner scopes keep brokers alive through their callback and join
  them on success/error (S13.3.1v2). Agent workflow fixtures use those scopes;
  their explicit cold reopens remain. A separate full fast-peer cold variant
  preserves that protocol's uncached negative and duplicate-delivery coverage.
- Cache regressions cover foreign-writer head invalidation, unchanged repeated
  observations, numeric type poisoning, explicit verification after corruption,
  cold saved-document reopening, and over-budget fallback.
- Compiler timing parsing uses named fields, accepts modern and legacy schema 1,
  preserves optional measurements, and rejects duplicates/overflow/truncation
  without partially updating its destination. Two native regressions pass.

The current merged tree also duplicated `dom_document_resolve_url` in Lambda
and Input, blocking LTO release linking. Lambda now calls the Input-owned API;
its duplicate selector implementation is removed (D9.1.1v4). This required no
sibling or vendor source change. The final build identity and qualification
results follow below.


## Final matched release qualification

Three runs per arm/test, AUTO, profiling disabled, no concurrent builds/tests or
local-S3 services. Both arms use the same frozen release executable above.
`baseline-home` and `baseline-tests` contain the pre-change HEAD sync library
and original fixtures; the after arm uses final working sources. An overlay
probe confirms independent package loading. Arm order alternates by round;
all 42 outputs match the original goldens. Values below are medians and full
observed ranges, with no sample discarded.

| Test | Before seconds (range) | After seconds (range) | Peak RSS before → after |
|---|---:|---:|---:|
| `sync_peer_burst` | 19.782 (19.700–19.842) | 4.816 (4.811–4.880) | 277.9 → 279.5 MiB |
| `sync_document_repository` | 7.469 (7.467–7.541) | 5.289 (5.280–5.419) | 289.7 → 308.4 MiB |
| `sync_metadata_batch` | 7.295 (7.214–7.395) | 0.975 (0.970–0.979) | 87.2 → 86.5 MiB |
| `sync_peer_fast` | 6.640 (6.600–6.661) | 1.659 (1.608–1.660) | 286.6 → 243.1 MiB |
| `sync_paged_publication` | 6.936 (6.815–7.234) | 1.255 (1.244–1.696) | 99.3 → 95.7 MiB |
| `sync_retention_repository` | 5.915 (5.858–6.031) | 3.780 (3.731–3.929) | 259.9 → 267.2 MiB |
| `sync_warm_protocol` | 5.406 (5.373–5.458) | 2.314 (2.305–2.315) | 312.2 → 258.9 MiB |

The sum of these seven serial medians falls **59.443 → 20.086 seconds (66.2%)**.
This sum does not predict the wall time of the parallel whole-project baseline.
The added cold fast-peer case remains about 6.5 s in release and about 7 s in the
full debug baseline; it is additional retained coverage, not a renamed omission.

Peak RSS is mixed: document rises about 18.7 MiB and retention 7.4 MiB, while
fast peer falls 43.5 MiB and warm protocol 53.3 MiB. The 1 MiB limit describes
canonical expanded-view bytes, not total runtime/compiler heap. This pass does
not claim the earlier process-memory budgets are met or that long-lived S3 idle
memory was newly qualified. Cache lifetime/size correctness and repeated stable
head observations are checked separately, including forced/poisoned precise GC.

### Final verification evidence

- `comparison-results.json`, `comparison-summary.json`, `comparison.log`: normal
  release timings, peaks and exact-output checks; library identity in
  `comparison-base-commit.txt` and `overlay-{before,after}.out`.
- `sync-goldens-auto.log`: 117/117 before the last fixture-only owner moves;
  all final owner fixtures also pass in the full baseline and backend checks.
- `interp-final.log`, `focused-jit.log`: 12/12 each. `cache-forced-gc.log`:
  cache regression passes with force-every-25 and freed-memory poisoning.
- `s3-protocol.log`, `s3-drop.log`: independent repositories, polling, revocation,
  snapshots, offline restore, checksum rejection, lost responses, killed-client
  resumption and interrupted object upload/retry all pass. The mixed demo's 11
  text/image/binary/empty files restore exactly with its sender offline.
- `timing-reader.log`: 2/2 native cases. `baseline-reruns.log`: 5/5, including
  those units and all three optional cloud cases with normal fixture settings.
  `timing-capture.tsv` contains real nonzero `analysis_us` values.
- `lambda-baseline.log`: 7599 passes, five initial failures. Three arose because
  the investigation runner accidentally retained the stopped integration S3
  endpoint; its environment isolation is corrected and clean reruns pass.
  `constrained-{before,after,previous-release}.err` and the native contract case
  identify the remaining Input LI-076 shared-core issue. No test was masked.
- `radiant-baseline.log`: 8170 pass / 297 partial / 20 skip / 1 failure.
  `radiant-rerun.log`, `outdoor-import-{previous,current}.err`: the independent
  outdoor default-argument compile failure. All 201 document and 329 UI cases
  pass. The source defect versus compiler-effect cause remains unconfirmed.

All files above are under `temp/sync-test-performance-impl/`. The old diagnostic
section and its sample/counter limitations remain historical evidence. Future
work can target uncached history hydration and remaining native/compiler cost;
this implementation does not require changing the saved-version/wire model.

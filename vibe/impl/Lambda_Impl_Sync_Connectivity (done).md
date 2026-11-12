# Automatic discovery and Internet peer connectivity

**Date:** 2026-11-12
**Status:** Implemented; local qualification complete; cross-NAT qualification pending
**Starting commit:** `c9acba516`, main checkout, `master`.
**Design:** [Data sync §4](../Lambda_Design_Data_Sync.md).
**Authority:** S12.1.1v3 (script policy), S12.4.4v4 and S13.1.2v2
(task ownership), D9.3.4 (packages), D11.1.2 and D11.2.1–D11.2.4
(native IO and grants), D9.1.1v4 / D9.1.3v2 (project ownership).

## Current checkpoint

Completed on 2026-11-12 in the main checkout, `master`. This phase implements
private Bonjour discovery, finite HTTP signaling, WebRTC DataChannels with
ICE/STUN, bounded direct attempts, periodic cloud fallback, recovery and status.
Policy stays Lambda Script (S12.1.1v3); native additions expose task-owned IO
mechanisms from macOS DNS-SD and a pinned, unmodified libdatachannel C API.
Existing peer authentication, repository objects and CRDT formats are reused.

Local qualification is complete. All 73 sync goldens and four focused GC/poison
cases pass on AUTO, interpreter and JIT. Actual isolated S3/fresh finite HTTP
functions pass the five signaling/fallback assertions on all three tiers.
Independent-process gates pass LAN discovery, a 17,570,000-byte binary,
offline merging and recovery after killing both agents mid-transfer. Actual
local UDP STUN produces a server-reflexive ICE candidate. Release checks pass,
including twenty additional independent reconnects. The aggregate Lambda
baseline passes **7,583/7,583**; final native deadline changes have separate
focused receipts below rather than an implied second aggregate run.

The user explicitly selected local checks with **cross-NAT verification pending**.
Physical-device, browser, hosted-provider and nonmacOS qualification remain
external. Two early raw RTC setup deadlines remain unconfirmed; subsequent
release workflows/reconnects pass, and no general timeout fix is claimed.
Automatic timeout fallback/retry remains part of the specified lifecycle.

All owned test children have exited. The isolated S3 backend PID 25219 was
verified, terminated and reaped; loopback port 49877 is closed. Evidence, source
and fixture hashes, binary identities and cleanup receipts are retained under
`temp/sync-connectivity/`. The commit includes only this phase. Starting HEAD
`c9acba516` advanced to `27326f215` through unrelated TeX work; that work and
subsequent unrelated AGENTS/shared-ledger edits remain untouched.

No further implementation is required for this phase's accepted local scope.
Cross-network physical-device qualification is the outstanding acceptance step.

## Delivered integration

- `lambda.io.rtc` provides bounded reliable ordered binary DataChannels,
  complete offer/answer descriptions, status counters and task cleanup.
  `lambda.io.discovery` provides finite private Bonjour advertise/browse calls.
  Callback queues contain native bytes only; precise roots own script carriers
  (S12.4.4v4, S13.1.2v2, D11.1.2 and D11.2.1–D11.2.4).
- `utils/prepare_sync_connectivity.py` builds unmodified libdatachannel v0.24.6,
  commit `6b1e2e620f1e37f0eafeee702eaea0043cb305fd`, and pinned submodules as
  static Release dependencies. Its provenance file records preparation inputs.
  No vendor or sibling source was modified (D9.1.1v4, D9.1.3v2).
- `rendezvous`, `rendezvous_function` and `connectivity` implement encrypted,
  signed, recipient/session/instance-bound signaling with expiry and monotonic
  CAS replacement. Three finite HTTP operations reuse existing cloud authority.
- `peer_transport` shares existing authenticated replication between WebSocket
  and DataChannel. `connectivity_agent` owns repository writes, rotates peers,
  retries failed connections, polls cloud storage and reports transfer status.
- [CONNECTIVITY.md](../../lmd/package/sync/CONNECTIVITY.md) documents preparation,
  enrollment, CLI usage, limits, trust, schedule, APIs and qualification scope.

## Verification and evidence

Receipts below are in `temp/sync-connectivity/`; `source-identities.json` records
248 source/fixture inputs plus this completed plan, and relevant executable
hashes. `qualification.json` indexes final results. Commands use
`LAMBDA_EXEC_BACKEND=auto|interp|jit` and `LAMBDA_SYNC_EXE` where appropriate.

| Command / acceptance | Result | Receipt |
|---|---|---|
| `make test-lambda-baseline` | 7,583/7,583; 19m35s; includes 368 MIR GC and 98 security cases | `baseline.log`, `baseline-summary.json` |
| `python3 test/sync_connectivity/test_goldens.py` | 73/73 on each tier | `post-baseline-goldens-{auto,interp,jit}.log` |
| Same command with `--gc` | 4/4 on each tier, GC/poison | `post-baseline-gc-{auto,interp,jit}.log` |
| `python3 test/sync_connectivity/test_signaling.py` with private S3 configuration | 5/5 on each tier: expiry/deadline fallback, recovery, fresh direct objects, cloud coverage, process replacement | `warm-final-signaling-{auto,interp}.log`, `warm-final-signaling-jit-2.log` |
| `python3 test/sync_connectivity/test_connectivity.py` | 3/3 on each tier; final native AUTO and Release reruns pass | `qualified-process-{interp,jit}.log`, `deadline-final-process.log`, `guard-release-process.log` |
| `python3 test/sync_connectivity/test_stun.py` | Actual local UDP STUN / server-reflexive candidate passes | `final-stun.log` |
| Process command with `LAMBDA_CONNECTIVITY_REPEATS=20` and Release executable | Original three gates plus twenty fresh reconnects pass | `release-repeat.log` |

The aggregate and all-tier package/GC receipts precede the last native Bonjour
absolute-deadline guard. After that guard, debug and Release builds, real LAN /
DataChannel / interruption gates, and actual local UDP STUN pass. The guard does
not change signaling or repository policy. No second aggregate run is claimed.
The final signaling tests use warmed external-edit helpers and the production
15-second connection budget; ordinary 60-second and GC 300-second fixture
budgets remain unchanged.

Final native debug `lambda.exe` SHA256:
`3c65044af2b12e7877c346fd54d1153cc146b06d9dda8a7f3480fdcf725bef98`.
Final archived Release `temp/sync-connectivity/lambda-release.exe` SHA256:
`242d85510e67aaa6f3880f0a1e8589158087b91fa523347b185a09b3da793696`.
The aggregate executable SHA256 was
`8e1afd84f034b3467cb9f9fb8821b071f8bf091b9d1fde6d796d400432514687`;
older debug/Release snapshots are separately identified, not final identities.

Final Release process measurement: discovery 1.97 seconds; complete channel
workflow 42.30 seconds; verified file 17,570,000 bytes; accepted DataChannel
messages 8,902,636 bytes sent and 112,146 received. The workflow includes capture
and exchange; its repetitive content deduplicates, so this is not a raw network
throughput measurement. Native counters exclude UDP/DTLS/SCTP overhead.
See `process-guard-release/measurements.json`.

The isolated SeaweedFS 4.53 S3 backend was owned only for this test run.
`backend.json` records its exact process; `backend-cleanup.json` records successful
termination/reaping and closed port. Initial launch attempts failed because an
inherited `DEBUG=release` conflicted with its boolean setting; the child
environment was corrected before use. Private configuration stays in ignored
mode-600 files. Process drivers reap client/function/helper children and proxies.

## Integration findings

- Bonjour's `NoAutoRename` flag requires a registration callback. The first
  native adapter omitted it and registration failed. The adapter now processes
  and confirms that callback, preserving the exact private discovery name.
- Shared historical sync fixtures contain fixed temporary paths. Parallel tier
  sweeps deleted another tier's `sync_local_binary` files. The connectivity
  runner now locks that shared test state; serial sweeps pass without changing
  fixtures, deadlines or native file capture.
- A newly merged peer frontier could inherit `cloud_saved:true` from the last
  cloud round. Coverage is now recomputed for progress, completed rounds and
  the final result. A long-interval integration gate admits fresh work after
  both fallback rounds finish, then verifies that direct delivery remains
  distinct from cloud persistence. This is script package policy (S12.1.1v3).
- Early raw RTC setup deadlines remain unconfirmed. All-tier ordinary/GC tests,
  successful release workflows and twenty independent process reconnects are
  recorded separately. Automatic fallback/retry is the specified connection
  lifecycle; no vendor change or general timeout fix is claimed.
- Sixteen maximum-sized signaling records exceeded the existing 1 MiB function
  response contract. Admission now caps each signed record at 120 KiB and reads
  at most eight records. The fixture admits a large legitimate ciphertext,
  checks the encoded eight-record response and rejects oversized records/pages.
- The coverage regression must introduce fresh work after the last cloud round.
  Its test-only proxy triggers a separate warmed capture worker before each new
  offer. Cold JIT helper compilation originally consumed the deliberately short
  attempt budget. Setup now finishes before negotiation, using the production
  15-second attempt bound. Normal 60-second and GC 300-second budgets are unchanged.
  A sender can legitimately publish during a retry; the assertion requires new
  objects to arrive directly and the new recipient to lack cloud acknowledgment.

- Bonjour's deadline calculation previously sampled the clock twice around an
  unsigned subtraction. Crossing the deadline could underflow a relative wait.
  The helper now accepts absolute deadlines and samples once before subtraction;
  registration, browse and resolve pass their existing finite budgets. Focused
  final native debug/Release process and STUN gates pass after this correction.

## Completed milestones and acceptance

1. **Native mechanisms.** Reproducible pinned dependency preparation; bounded
   reliable ordered data channels with task cleanup, cancellation and precise
   GC ownership. Bonjour advertisements/browse results are advisory only.
   Test actual data transfer, malformed options, deadlines and cleanup.
2. **Script protocol.** Signed expiring presence, recipient-bound encrypted
   connection offers/answers, replay/session binding, bounded service storage,
   membership admission and monotonic replacement. Test tampering, expiry,
   stale writers, wrong peer/space/session and revocation.
3. **Transport integration.** Share existing peer authentication/exchange over
   WebSocket and DataChannel. Add discovery/signaling orchestration, bounded
   retries and explicit status. Serialize repository writers. Direct success
   must not be mistaken for cloud persistence.
4. **Qualification.** Independent agents, real service requests, LAN discovery,
   direct transfer, failed-connect fallback, recovery, offline edits and process
   interruption. Use existing 60-second normal / 300-second focused GC budgets.
   Run sync goldens across AUTO/interpreter/JIT, focused ownership checks and
   aggregate Lambda baseline at completion. Use a release build for measurements.

Evidence goes in `temp/sync-connectivity/`. Record source and binary identities,
exact commands, assertions, resource cleanup and any unqualified environment.
The local milestone is complete; keep external qualification results separate.

## References

- [ICE, RFC 8445](https://www.rfc-editor.org/rfc/rfc8445)
- [DataChannels, RFC 8831](https://www.rfc-editor.org/rfc/rfc8831)
- [libdatachannel C API](https://libdatachannel.org/pages/reference.html)
- [libdatachannel v0.24.6](https://github.com/paullouisageneau/libdatachannel/releases/tag/v0.24.6)

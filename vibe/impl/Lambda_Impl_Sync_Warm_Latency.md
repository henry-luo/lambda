# Warm LAN sync latency

**Date:** 2026-11-14
**Status:** Tuning iteration verified; subsecond delivery at 100 files remains open.
**Worktree:** Main checkout, `master`; resumed at `f98946625`. Earlier work
started at `1118f5108`, with verification base `2585cffa5` plus that change set.
**Authority:** S12.1.1v3 (script policy), S13.1.2v2 / S13.3.1v2 (tasks),
D6.3 (precise roots), D9.3.4 (native IO), D11.1.2 (anchored IO).
**Design:** [Data sync §2.1.1](../Lambda_Design_Data_Sync.md#211-lan-change-detection-and-connection-bursts).

## Current checkpoint

- Resumed 2026-11-13, updated 2026-11-14, main checkout `master`, `f98946625`.
  The initial working tree was clean. All changes are Lambda-owned scripts,
  tests and documentation; native/sibling/vendor source is unchanged.
- Release host built with `make build-release-compile JOBS=4`, frozen as
  `temp/sync-warm-latency/resume/lambda-release.exe`. Evidence in that directory.
- Current changes: exact primitive codec fast paths, indexed causal-ID/path
  lookup, indexed missing-object queries, removal of installed transaction
  packet prefixes before receive, reused validated projections, and batched
  installation progress under the previously committed complete intent.
- Authentication, immutable file revisions, physical object verification and
  ten quiet seconds remain. No wire/storage format migration is introduced.
- Before (100 seed files, three samples/mode): completed delivery medians
  3.745 s settled / 4.554 s continuous (`before-100.json`). Codec/packet candidate
  improved these to 2.202 / 2.413 s (`scalars-100.json`), still above the gate.
  These candidate results predate the current indexes and projection reuse.
- Before the capture-race fix, ordinary sync sweep: **267/267** passed, 89 each
  on AUTO/interpreter/JIT; Lambda baseline **7,478/7,478** passed.
  Focused GC: six small cases pass with collection every allocation; six
  integration cases pass every 32 allocations, all with poisoning. Every-allocation
  integration recovery timed out at 300 s; that stronger qualification stays open.
- Native peer and WebRTC process tests pass before the retry fix, including the
  17,570,000-byte binary and interrupted-transfer restart. WL-01 is confirmed:
  a directory token changed during final readback and the error closed the burst.
  Typed bounded capture retries are now implemented; the unchanged LAN gate
  passes three isolated runs (`resume/lan-fixed-{1,2,3}.log`). Current-source
  focused checks pass **21/21** across AUTO/interpreter/JIT, plus **3/3** new
  capture-error checks with GC/poisoning every 32 allocations.
- Added legacy codec byte vectors, indexed-ID parity, installed-packet history /
  checkpoint fallback / equivocation checks, and rollback-after-effects recovery
  with an external edit. Each new script has its exact-output golden.
- A benchmark now records source hashes and rejects concurrent source changes.
  Keep timing runs separate from builds/tests. Physical-device timing remains
  unqualified; this run measures independent processes on one macOS host.
- Current-source timing: 10 files / two peers, 20 edits, maximum completed install
  **0.640 s**; 10 files / three peers, 10 edits, maximum **0.739 s**. Both pass.
  At 100 files medians improve to **1.455 / 1.555 s**, but still fail one second.
  `resume/qualified-*.json` record unchanged source hashes; all use AUTO.
- The tested release host is restored after the baseline. An unrelated user
  edit to `AGENTS.md` is preserved; its new rule 21 reinforces selective testing.
  The small retry follow-up uses focused checks instead of another full baseline.
- Verification is complete for this iteration. Next: reduce repeated whole-state
  validation/serialization with incremental metadata storage and validated reuse;
  preserve typed integrity and recovery, then rerun the unchanged 100-file gate.

## Previous wrap-up

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

### 2026-11-14 tuning

The current-code release measurement reproduced the scale problem at 100 seed
files with 4 KiB edits. `resume/before-identity.json` identifies the source and
executable; `resume/before-100.json` separates capture, visibility and completed
installation. The one-second acceptance gate failed, as expected.

The stage copies under `resume/instrumented/` add diagnostic IO and are separate
from the production package. Their durations identify repeated work, not a
replacement for uninstrumented acceptance. `resume/profile-before.jsonl` shows
repeated full transaction validation and head serialization during installation.
Primitive encoding also invokes Mark format/parse for every null, boolean and
finite int53, despite their exact representations under S4.1.1. The new codec preserves
legacy typed bytes and retains scalar round trips for the other numeric and
document kinds. Decode still requires exact canonical re-encoding. Shared
infinities retain their legacy wire forms, and NaN remains rejected by the
lossless check (S4.2.1–S4.2.3).

Installation previously committed a full head after every guarded filesystem
operation and again after completion. `materialize` now applies its bounded
steps within one progress transaction, after the complete intent has committed
separately. A failure can leave filesystem effects with the old progress cursor;
recovery repeats the guarded effects. `install_step` still advances one step.
The rollback fixture verifies this case and checks that a subsequent external
edit blocks replay and retains its recovery capture. This does not claim atomic
visibility of a filesystem tree.

Receiving a new workspace packet drops only operation-log/journal groups covered
by the accepted disk baseline. Atoms, causal receipts and immutable file revisions
remain; latest atomic groups remain available for relay. Stale frontiers use the
existing signed-checkpoint fallback. Missing-object queries use bounded indexed
SQL on unretired repositories; retired inventories retain reachability filtering.

The first-return linear lookup experiment regressed the isolated capture probe
and was removed. The later causal-ID lookup uses binary search only for sorted
causal indexes; generic unsorted lookup is unchanged. Installation and import
path loops build one checked path index per collection. Projection reuse is tied
to a validated, typed replica digest after external reconciliation (D11.1.2).

Current-source release timings use 4 KiB edits, AUTO, unchanged source hashes and the
same frozen executable (`c25d420ba52a4669811c500069734b86eac1795971cda3dfdab7a93c50384089`).
Three-peer rows report arrival/installation at the slower destination.

| Seed files / peers / mode | Samples | Visibility median | Installed median | Installed maximum |
|---|---:|---:|---:|---:|
| 10 / 2 / settled | 10 | 0.509 s | 0.555 s | 0.630 s |
| 10 / 2 / continuous | 10 | 0.527 s | 0.598 s | 0.640 s |
| 10 / 3 / settled | 5 | 0.664 s | 0.720 s | 0.735 s |
| 10 / 3 / continuous | 5 | 0.646 s | 0.706 s | 0.739 s |
| 100 / 2 / settled | 3 | 1.260 s | 1.455 s | 1.560 s |
| 100 / 2 / continuous | 3 | 1.344 s | 1.555 s | 1.588 s |

Reports: `resume/qualified-10.json`, `resume/qualified-three-peer.json`, and
`resume/qualified-100.json`. Against the matched three-sample before run, 100-file
installed medians improve by 61% settled and 66% continuous. The 100-file gate
still fails; small-workspace results do not qualify subsecond delivery at scale.
These are independent processes on one host, not physical LAN-device timings.

#### WL-01 — External capture race closed a warm burst

The unchanged LAN event gate intermittently failed its warm connection count
assertion. The isolated timeline in
`resume/lan-diagnostic-95205/timeline.json` shows the receiver raising
`sync filesystem: directory changed during scan`, followed by the sender's
`connection ended before acknowledgment` and a fresh connection attempt. The
reverse-direction edit was made while the receiver completed installation's
readback. This is a confirmed package orchestration defect, not an unproven
handshake-readiness explanation. The original assertion is unchanged.

`filesystem` now classifies detected snapshot changes with application error
8318 (S7.4.4), then retries a fresh bounded capture up to three times by default
(caller budget 1–16). Native read failures are classified as raced captures only
when a fresh observation differs from the saved token. Cancellation is forwarded
immediately; unsafe/root replacement and other failures retain their original
outcomes (D11.1.2). Immutable objects from an abandoned attempt can be reused;
no local head or accepted baseline is published until a capture succeeds.

The new `sync_capture_errors` fixture checks stale/deleted file classification,
size/root/budget failures and unchanged generation before a later successful
capture. The real concurrent readback/reuse case remains the independent LAN
process test. Three isolated runs now pass with the original assertion, each
about 56 seconds, including startup, quiet close, reverse edits, moves, atomic
saves, deletion, restart and fan-out. Post-fix focused tier/GC and timing results
are recorded separately; the earlier aggregate baseline predates this small fix.
`resume/qualification.log` records 21 focused normal runs and three sampled-GC
runs after the fix. The five new fixtures all have expected-output files. The
final release host is restored and its SHA matches the measured executable.

### Previous investigation

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

### Current iteration

`resume/verify.py` completed 267 exact goldens before the retry fix, then stopped
at the GC deadline below. `resume/verify_remaining.py` completed focused GC,
native peer/WebRTC tests and the 7,478-test baseline; its initial LAN failure
led to WL-01. After the fix, the unchanged LAN gate passes three isolated runs.
`resume/qualify.py` completes 21 focused normal and three GC32 runs across all
tiers, then the three release latency workloads. `resume/qualified-identity.json`
and each timing report retain the exact tested inputs. Package/harness source
hashes reject edits during measurement. The measured release host is restored
after the baseline's debug build and its executable identity has been checked.

Full collection after every allocation exceeded the existing 300-second budget
on AUTO `sync_install_batch_recovery`. `resume/verification.log` retains the
timeout; no fault or output mismatch was observed before termination. Full-GC
qualification for this integration fixture remains open. This is a stress-budget
limit, not evidence that the runtime cause has been diagnosed. The follow-up
`resume/verify_remaining.py` retains every-allocation collection/poisoning for the
small codec/index fixtures and uses every 32 allocations for integration recovery
checks on all three tiers. Record those outcomes with their actual intervals;
they do not erase or qualify the timed-out every-allocation run. The ordinary
and latency gates are unchanged.

```sh
LAMBDA_SYNC_EXE="$PWD/temp/sync-warm-latency/resume/lambda-release.exe" python3 test/sync_connectivity/benchmark_warm.py --seed-files 10 --samples 10 --report temp/sync-warm-latency/resume/qualified-10.json
LAMBDA_SYNC_EXE="$PWD/temp/sync-warm-latency/resume/lambda-release.exe" python3 test/sync_connectivity/benchmark_warm.py --seed-files 100 --samples 3 --report temp/sync-warm-latency/resume/qualified-100.json
LAMBDA_SYNC_EXE="$PWD/temp/sync-warm-latency/resume/lambda-release.exe" python3 test/sync_connectivity/benchmark_warm.py --seed-files 10 --samples 5 --peers 3 --report temp/sync-warm-latency/resume/qualified-three-peer.json
```

The 100-file command is expected to fail its unchanged one-second acceptance
threshold on the current implementation; its report remains useful evidence.
Further work should isolate canonical whole-head/baseline encoding and repeated
validation, then select an incremental storage/validation design with the same
recovery and typed-integrity guarantees. Larger workspaces, sustained long runs,
physical devices and other operating systems need separate qualification.

### Previous wrap-up checks

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

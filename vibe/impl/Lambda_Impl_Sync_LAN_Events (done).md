# LAN event monitoring and connection bursts

**Date:** 2026-11-13
**Status:** Complete for the native macOS LAN milestone; qualification limits below.
**Starting tree:** `e5d5e848e`, main checkout, `master`; initially clean.
**Authority:** S12.1.1v3 (effects), S13.1.2v2 / S13.3.1v2 (tasks and
ownership), D6.3 (precise roots), D9.3.4 (native IO modules), D11.1.2
(anchored IO), D9.1.1v4 / D9.1.3v2 (project boundaries).
**Design:** [Data sync §2.1.1](../Lambda_Design_Data_Sync.md#211-lan-change-detection-and-connection-bursts).

**Subsequent work:** [Warm latency investigation](Lambda_Impl_Sync_Warm_Latency.md)
changes batching defaults and adds burst v2. The defaults and measurements below
describe this completed milestone, not qualification of that later implementation.

## Current checkpoint

- Scope implemented: OS notifications, affected-subtree reconciliation, bounded
  batching, authenticated multi-round LAN bursts, idle closure and peer fan-out.
  Startup/restart, recovery and periodic content-verification scans remain.
- Initial bidirectional catch-up works without a new watcher event. Register the
  monitor before inventory so events racing startup stay queued.
- Defaults: 200 ms debounce, 1 second maximum batch delay, 10 second idle timeout,
  60 second maximum burst and 60 second verification/anti-entropy.
- Policy remains Lambda Script. New native APIs are task-owned `disk.watch`,
  `disk.changes` and `disk.unwatch`, using existing libuv OS notifications and
  anchored IO. No sibling or vendor sources were modified.
- One script owner serializes repository effects; network waits hold no repository
  transaction. Its private cache authenticates the SQL head before reusing the
  same generation/object ID. Mutations still validate outputs, and committed
  cache/token publication follows transaction completion.
- Content fingerprints suppress covered notices. Equal-epoch causal coverage
  skips redundant workspace checkpoints while objects, documents and retention
  metadata still reconcile. Legacy finite CLI/RTC/cloud profiles remain compatible.
- Initial milestone checks passed: 82 exact sync goldens on AUTO/interpreter/JIT;
  seven-case native LAN acceptance on each tier; demo, finite peer and native
  connectivity process suites. Normal/cancelled agent shutdown passes.
- One interpreter connection-reuse assertion failed during a concurrent release
  build. Three unchanged isolated repetitions pass. Preserve both the failure and
  repeats; timing sensitivity under CPU contention remains a qualification limit.
- Aggregate Lambda baseline: **7,467/7,467 passed**. Native watcher/discovery
  forced/poisoned GC: both fixtures pass on AUTO/interpreter/JIT (48 assertions).
  These broad checks used the original three-second idle default. The ten-second
  follow-up checks below qualify the configuration change.
- Evidence: `temp/sync-lan-events/verification-final-normal.log`,
  `verification-final-rest.log`, `verification-aggregate-driver.log` and
  `final-inputs.json`. The first driver stopped at the loaded interpreter case;
  the second records the isolated repeats and remaining successful gates.
- Starting HEAD was `e5d5e848e`; the shared checkout advanced to `569eb6316`
  through unrelated LaTeX commits. Concurrent LaTeX edits remain untouched.
  Source/executable hashes identify this task independently of that activity.
- Final debug SHA-256: `9397ef1998e7eb79929d61d7bfd4cebcab1a0657799c3620e7a20bce0e708348`.
  Final release SHA-256: `6241e7a8bc461958309a4933c9a4be78ce584ea5726e8b617cb9b490294e2e2b`.
- Qualification host is native macOS. Physical-device, cross-NAT and other-OS
  qualification remain separate. Full timed filesystem bursts under allocation-
  forced GC are not qualified (SD-05); focused native-handle GC checks are separate.

## Milestones and acceptance

1. Native directory notifications and task lifecycle: nested changes, rename,
   replacement, deletion, bounded hints, explicit close and cancellation.
2. Reconciliation: incremental capture matches a full capture; moves preserve
   identity; failed observations do not replace the accepted baseline.
3. Burst protocol: one authentication covers several rounds; either side can
   announce changes; encryption sequence and current-member checks persist;
   idle closure and interrupted transfer preserve pending work.
4. Agent/demo: startup catch-up, online external changes, simultaneous writers,
   newly created subdirectories, idle scan suppression, connection reuse/close,
   fan-out and restart/offline convergence.
5. Verify focused sync goldens on AUTO/interpreter/JIT, focused GC stress,
   independent native processes, then the aggregate Lambda baseline once.
   No latency claim from a debug executable; release measurements are separate.

## Implementation notes

Native callbacks retain only native bytes. Script values are created on the
scheduler thread under precise roots. Cancellation closes native watchers and
joins connection tasks. Filesystem notifications never authorize file access;
existing anchored capture/revision validation remains authoritative.

One script owner serializes repository callbacks. Network workers park outside
repository scopes, so simultaneous incoming/outgoing connections cannot deadlock
on a repository lease. Installation is serialized as one recovery operation.

## Release latency

Release AUTO measurement after the seven-case, three-process history scenario:
five cold deliveries 1.53–2.55 seconds (median 2.05), five warm deliveries
2.76–4.47 seconds (median 3.28). The observer checks every 50 ms. These are
independent processes on one macOS host, not physical-device latency claims.
History serialization and validation still cost CPU; subsecond delivery is not
established. Evidence: `latency-head-cache.log`, `latency.json`. The measured
release binary SHA-256 is
`35cffbe808e60caee953464ea916f3990baa454d09a124a33edb07c13f06e222`.
It predates the final Windows hint-separator and Linux allocation-failure checks.
It uses the original three-second idle setting. The follow-up changes that default
to ten seconds; latency measurements above have not been repeated. No debug
timing is reported.

## Issues and verification

- **SD-04 — Unrelated edits blocked completed installation (fixed).** The
  online reverse-edit test created a new file after a received file appeared but
  before journal completion. Final tree equality rejected that addition and
  subsequent scans refused the pending journal. Completion now establishes the
  installed baseline and imports unrelated additions/edits only if every operation
  target still has its exact intended type/content. Changed targets still block and preserve
  recovery captures. `sync_install_additions` exercises the exact boundary.
- Repository RPC replies use a private child mailbox, preserving unrelated
  agent events (S13.2.1). Scheduling uses monotonic `clock()`, not the coarser
  wall-clock datetime projection. Empty maps in procedural conditional branches
  are parenthesized as required by S16.4.2v2.
- The final 82-fixture exact sweep supersedes earlier intermediate sweeps.
  Sandbox execution cannot receive macOS FSEvents reliably; native qualification
  ran outside that sandbox.
- Existing core issue [LR02-38](../Lambda_Issue_Ledger.md#lr02-38) was rechecked
  with a minimal rejected imported module: failed AST inspection retains two
  native system allocations / 262 bytes. The existing ledger entry now carries
  that evidence. Valid execution is unaffected; no compiler workaround was added.
- Bounded acknowledgments on watcher/discovery event batches prevent a busy
  repository capture from overflowing the agent mailbox. Final exact/process
  gates include this refinement. Native queues retain/coalesce waiting events
  and request a full scan on overflow.
- **[LR02-39](<../Lambda_Issue_Ledger (fixed).md#lr02-39>) — Bonjour resolution starvation (fixed).** Unfiltered native browsing could
  spend the entire remaining window on an unrelated unresolved instance. OS
  diagnostics listed both new peers, while native discovery returned zero rows.
  Added exact-name/prefix filters before resolution and fair candidate deadlines.
  The six-assertion native filter fixture passes, and initial/online LAN delivery
  works in the same environment with the unrelated entries still present.
- The combined capture fixture originally retained entire loaded repository
  states and hit the 300-second forced-GC deadline. It now projects only the
  version IDs used by its assertions. All eight assertions pass under the same
  force-every-allocation/poison settings and 300-second limit; see
  `capture-gc-projected.log` / `.err`. No production GC behavior or budget changed.
- **SD-05 — Full burst integration under allocation-forced GC (qualification
  limit).** Before head caching, the full multi-round filesystem scenario exceeded
  the 300-second stress deadline. After caching it finishes with a closed-channel
  error under forced collection. Interaction with its 10-second burst/500 ms
  idle scheduling bounds is suspected; the exact close path is unconfirmed.
  Normal execution passes; no memory corruption was observed. Preserve `verification-gc.log`, `burst-cache-gc.log` / `.err`.
  Native-handle GC stress and ordinary timed integration are separate gates;
  this full timed scenario is not qualified under allocation-forced GC.

## Initial milestone validation

| Gate | Result | Evidence under `temp/sync-lan-events/` |
|---|---|---|
| Debug and release builds | Pass | `build-final-native.log`, `build-final-release.log` |
| Exact sync goldens | 82 on each of AUTO/interpreter/JIT | `revised-goldens-*.log` |
| Native LAN process acceptance | Seven cases per tier; three isolated interpreter repeats pass | `final-lan-*.log` |
| Demo enrollment and file sync | Pass | `final-demo.log` |
| Existing finite peer protocol | Pass, including established-session revocation | `final-peer.log` |
| Native Bonjour/RTC/restart recovery | Pass | `final-connectivity.log` |
| Watcher/discovery allocation-forced GC and poisoning | Both fixtures pass on all three tiers, unchanged 300-second budget | `final-gc-*.log`, `.err` |
| Aggregate Lambda baseline | 7,467/7,467, exit 0 | `baseline-final.log` |
| Local document links, Python syntax and diff whitespace | Pass | Final source review |

The final root `lambda.exe` uses the release build above. Run the upgraded
`./lambda.exe demo sync` on both devices; existing pairings and working folders
are reused. Two physical devices and other operating systems still need their
own qualification. SD-05 remains an explicit stress-scope limit, not a claim
that full filesystem bursts pass allocation-forced GC.

## Ten-second idle follow-up — 2026-11-13

The user requested ten quiet seconds before closing a LAN connection. Changed
`connectivity_agent.defaults.idle_ms` from 3,000 to 10,000 and updated the design
and package/demo guides. The LAN process gate now uses the actual default instead
of overriding it. Custom short test settings remain explicit overrides.

Validation: all seven native LAN process cases pass with the ten-second default,
including connection reuse, idle closure, restart/offline recovery and fan-out.
The connectivity protocol, LAN lifecycle and burst fixtures pass exact goldens on
AUTO/interpreter/JIT (nine focused checks). Python syntax and diff whitespace
checks pass. No native code changed after the broad milestone qualification;
the existing release binary loads the updated scripts at launch.

Evidence: `temp/sync-lan-events/idle10-lan-auto.log`, `idle10-focused.log` and
`idle10-inputs.json`. The earlier latency measurements and broad baseline results
remain identified with their original configuration; no new latency claim is made.

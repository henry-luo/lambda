# One-command LAN sync demo

**Date:** 2026-11-12
**Status:** Implemented; local qualification complete.
**Starting commit:** `3ffe3477a`, main checkout, `master`.
**Authority:** S12.1.1v3 (script policy), S12.4.4v4 / S13.1.2v2
(task ownership), D9.3.4 (packages), D11.1.2 / D11.2.1–D11.2.4
(native IO, host grants), D9.1.1v4 / D9.1.3v2 (source ownership).

## Completion record

`lambda demo sync` opens a native window and defaults to `$HOME/lambda-sync`.
The first device enters a user ID and creates a sync space; the other enters
that ID and the first device's one-time pairing code. Discovery, encrypted
enrollment, local grants, scans and bidirectional synchronization are automatic.
Saved profiles reconnect without entering another code. The user explicitly
selected a one-time code rather than a shared phrase.

The demo provides create/link navigation, code copying, live public progress,
open-folder, pause/resume, another-device enrollment, cancellation and errors.
Wrapping keys use the existing login Keychain API; public settings and encrypted
repositories live outside the synced folder. Public user IDs select discovery
candidates and never grant access. Production account sign-in can replace this
demo bootstrap (D11.2.3). The supported initial host is native macOS; no cloud,
S3, STUN or optional WebRTC preparation is required for this LAN profile.

Protocol, profile lifecycle, controller and UI are Lambda Script (S12.1.1v3).
Production C++ changes only register the existing demo catalog entry. The other
C++ change registers a Lambda-owned native UI regression. No sibling or vendor
source was modified; the authorized Radiant issue-ledger report is listed below.
Unrelated TeX, AGENTS and shared-ledger edits remain untouched.

Local qualification is complete: 76 exact sync goldens on AUTO/interpreter/JIT;
six focused GC cases on each tier; seven real-process demo assertions on each
tier; two registered UI/catalog cases (12 form assertions, forced/poisoned GC);
and the aggregate Lambda baseline **7,598/7,598**. Final installation-page
handling was checked separately after that aggregate, as detailed below.
Physical-device, cross-NAT and other-OS qualification remain pending. The tests
do not create entries in the user's Keychain; the production host reuses the
existing OS secret-store API, while controller tests supply isolated keys.

No further implementation is required for this selected demo milestone.
Evidence, source/fixture identities and cleanup receipts are under
`temp/sync-demo/`. [User flow and implementation limits](../../test/demo/sync/README.md).

## Implementation

- `lambda.sync.pairing` defines account hints, code normalization, fresh
  challenges and role-bound AEAD proofs over both signed device descriptors,
  nonces and full account digest. It reuses existing signed, recipient-encrypted
  invitations. Codes contain 80 random bits, expire after two minutes, and
  authorize one device. Native authentication verifies proofs (D11.1.2).
- Pairing has eight-attempt bounds, 16 KiB messages and at most five seconds per
  receive. The host spends a code on the first valid proof, including when the
  final receipt is lost. Codes stay in memory/UI rather than profile or status
  files. Account hash prefixes are advisory discovery hints, not credentials.
- `mod_profile` preserves independent device identities and repositories, rejects
  account/folder substitution, and recovers committed enrollment metadata after
  a lost receipt. The OS-vault adapter owns a per-profile/user wrapping key.
- `mod_agent` owns enrollment or ordinary sync, never both simultaneously. It
  rotates discovery candidates and retries transient failures within the same
  attempt/deadline bounds. Successful enrollment resumes the ordinary agent.
- `connectivity_agent.run` accepts an optional public status observer for UI
  integration. A shared `connectivity.hint_url` validates untrusted Bonjour
  hints. Task completion/cancellation releases native listeners/advertisements
  (S12.4.4v4, S13.1.2v2).
- `peer_agent.resume_agent` resumes a durable installation before scans. It
  drains successive bounded pages until complete, blocked or unable to progress.
  External races retain their captured recovery versions and require review.
- The CLI catalog, demo index tile and vector icon make the demo discoverable.
  Release preparation already copies the whole demo tree.

## Verification

All paths below are relative to `temp/sync-demo/`. The tested base is
`3ffe3477a` plus this phase's recorded working-tree inputs; `source-identities.json`
records final source/fixture and native executable hashes. Existing ordinary
60-second and focused GC 300-second fixture budgets were preserved.

| Gate | Result | Receipt |
|---|---|---|
| `make test-lambda-baseline` | 7,598/7,598; 24m30s; includes 2,486 Lambda cases and 368 MIR GC cases | `baseline.log`, `baseline-summary.json` |
| `python3 test/sync_connectivity/test_goldens.py` per tier | 76/76 on AUTO, interpreter and JIT | `goldens-{auto,interp}.log`, `serial-goldens-jit.log` |
| Same command with `--gc` | 6/6 per tier | `gc-{auto,interp}.log`, `serial-gc-jit.log` |
| `python3 test/demo/sync/test_sync.py` per tier | Seven assertions each; same-account enrollment, bidirectional external edits, offline move/restart, fresh codes, user isolation, bounded candidate retries and spent-code rejection, additional device | `retry-process-{auto,interp,jit}.log` |
| Final-source demo process rerun | Seven assertions pass | `final-acceptance-auto.log` |
| Final-source recovery under forced/poisoned GC per tier | Four assertions pass each | `final-recovery-{auto,interp,jit}/result.log`, `final-recovery-driver.log` |
| Registered `RadiantViewTest.SyncDemoFirstRunAndPairingForm` and `DemoCommandListsNamesAndRejectsUnknownNames` | Two cases pass; 12 form assertions, isolated paths, no Keychain writes | `final-ui.log` |
| Public setup/pairing/active/review/error view rendering | Six states render successfully | `render-states.ls`, `render-states.log` |

The aggregate preceded the final helper change that drains successive
materialization pages. After that change, the complete JIT normal/GC sweeps,
all-tier focused recovery GC checks and the AUTO demo process gate pass.
AUTO/interpreter complete sweeps qualify their earlier single-page helper;
the final-source recovery receipts qualify the changed helper on both tiers.
No second aggregate run is claimed.

The first JIT sweep overlapped the baseline's shared fixed fixture directories
and returned an SQLite rollback in `sync_local_filesystem`. The baseline exercised
that same fixture and passed. The JIT sweep was repeated only after the aggregate
finished and passed unchanged deadlines. Preserve `goldens-jit.log` and
`golden-sweep.log` as initial failure evidence. New recovery fixtures use unique
process-owned directories. Existing shared fixtures must still run serially
between a sweep and the aggregate baseline.

Builds: `make build` succeeds; the focused native UI target builds successfully.
The unrelated aggregate `make build-test` remains unqualified because of RI-020.
Python syntax, JSON fixtures and local Markdown links pass; final tracked and
untracked additions are included in the whitespace/source checks. All owned
process drivers reap children; `cleanup.json` records the final process check.

## Findings and remaining boundaries

- **SD-01 — Resolved startup recovery.** An agent stopped after a file appeared
  but before installation committed could fail on restart with `installation
  must finish before scanning`. The shared resume helper now completes/resumes
  that intent first. A blocked partial install reports review and preserves
  captured external bytes. Automatic rebase of such a blocked partial install
  remains an existing repository limitation. Offline tests wait for completed
  exchanges before making ordinary offline edits; separate tests construct
  pending/raced journals explicitly.
- **SD-03 — Resolved pairing retry.** Retaining every failed candidate URL for
  the entire enrollment window prevented retries after transient network errors
  or a first-use firewall prompt. The controller now rotates/retries endpoints
  within its existing bounds. The native wrong-code case confirms at least two
  attempts while retaining the same membership and rejecting enrollment.
- **RI-020 — Unrelated aggregate test-build failure.** The Radiant-owned native
  input-value test references `_process_css_resource` without its target linking
  that implementation. Recorded in the [owning ledger](../../../radiant/vibe/Radiant_Issue_Ledger.md#ri-020)
  with evidence `build-test.log`. The intended independent-test dependency
  closure remains unconfirmed. No sibling source or test workaround was applied;
  the unaffected demo UI target and separate Lambda baseline pass.
- **LR02-38 — Nonblocking rejected AST-dump leak.** A minimal invalid view
  initializer reports four live system allocations / 441 bytes after failed
  `--emit-ast-dump`; the original imported variant reported 543 bytes. Recorded
  in the [Lambda ledger](../Lambda_Issue_Ledger.md#lr02-38), allocation owner/cause
  unconfirmed. This does not affect valid demo execution. Only that appended
  entry belongs to this phase: do not stage other shared-ledger changes.
- The profile supports one active account/folder and one running owner. Device
  enrollment grants the existing full-space writer/checkpoint authority. OS-vault
  prompts, existing conflict resolution and blocked partial-install review are
  retained. There is no fake production sign-in service or cloud deployment.

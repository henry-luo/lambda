# Authenticated native peer file synchronization

**Date:** 2026-11-11
**Status:** Complete for the native LAN profile, with the qualification limits below.
**Design:** [Data sync §§4, 12.1](../Lambda_Design_Data_Sync.md),
[document/workspace protocol](../Lambda_Design_CRDT_Protocol.md).
**Spec linkage:** S12.1.1v3 (pure policy/procedural effects), S13.1.2v2
(task-owned network waits), D9.3.4 (packages), D11.1.2 and D11.2.1–D11.2.4
(host authority and network grants), D6.3.3 (precise roots).

## Completion record

Implemented 2026-11-11 in `/Users/henryluo/Projects/lambda`, branch `master`,
starting at `490f67361`; qualified on `b43c3896a` plus the recorded working-tree
inputs. Existing unrelated TeX, syntax, numeric and build work was preserved.
The peer-sync commit excludes that unrelated work.

- Delivered six peer modules, including the runnable CLI. Policy, pairing,
  invitations, encrypted sessions, checkpoint/object exchange, current-grant checks,
  conflict reporting and periodic retry remain Lambda Script (S12.1.1v3).
- Native changes add raw Ed25519/X25519 mechanisms and explicit IPv4 WebSocket
  binding. The LAN adapter reuses existing transports, not a new native CRDT engine.
  WebRTC/ICE, discovery and cloud adapters remain later integrations.
- Five new executable goldens have expected files; `_interrupt.ls` is an explicitly
  selected process-test helper. The process driver tests independent repositories,
  processes, folders and wrapping keys, including the actual non-loopback interface.
- **12/12 process acceptance checks on interpreter, AUTO and JIT.** Evidence:
  `temp/sync-peer/accepted-process-*.log`. After the baseline rebuild, AUTO repeated
  **12/12** in `rebuilt-process-auto.log`. No two-physical-machine or other-OS
  qualification is claimed.
- **6/6 focused tests per tier**, normal and every-allocation GC/freed-memory
  poisoning: `qualified-{normal,stress}-*.log`. The rebuilt host repeated **6/6**
  stress tests on every tier (`rebuilt-stress-*.log`). This includes a real encrypted
  channel with a suspending current-grant callback. Earlier wider 51-test sweeps
  passed on every tier; final focused receipts supersede their edited peer inputs.
- Existing WebSocket interoperability: **10/10**. Package structure: **868 scripts**.
  Scoped whitespace, fixture goldens and documentation links pass.
- Full Lambda baseline: **7,475/7,476**, all 40 suites attempted; runtime
  **2,364/2,365**, native MIR GC **368/368**. The sole failure is the independently
  reproduced TeX/expl3 package-echo golden discrepancy, recorded in the Lambda
  ledger. Neither TeX source/golden nor the baseline harness was modified.
- Rebuilt debug host SHA256:
  `13eca632e6740d1d3a5e692fb34c4ca96c14ae5c388917506ccd061d42c1a4e8`.
  Source and executable identities: `qualification-inputs.json`; earlier host:
  `pre-baseline-qualification-inputs.json`. Baseline log: `baseline.log`;
  isolated TeX failure: `expl3-recheck.log` / `.json`.
- After qualification, concurrent unrelated edits changed
  lambda-data-runtime.cpp, lambda-eval.cpp and vmap.cpp. They were preserved
  and not rebuilt or qualified by this task. The peer inputs and tested executable
  remain unchanged; post_qualification_concurrent_edits records their hashes.
- Fixed and archived **LR12-53**, the draft obsolete-membership overwrite.
  Additional simultaneous administrative-writer qualification is **blocked on
  input LI-015**. Its failed receipts and `test_peer.py --contention` remain;
  no owner source edits, SQL lock tricks or uncertain-commit retries were added.
  Supported administration is sequential. This does not block independent peers.
- Earlier intermediate source-edit/stress and fixture-syntax failures are superseded
  by the accepted stable-input receipts, not counted as successful qualification.

Public setup/API/protocol/limits: [native peer guide](../../lmd/package/sync/PEER.md).
The application handshake remains experimental and unaudited. Local power-loss
barriers remain unqualified; peer ACK never means cloud persistence. Retention,
OS key storage, file-only/read-only grants and editor integration remain later work.

## Milestones

1. **Done — native crypto and LAN binding.** Known RFC 8032/7748 vectors, invalid-key and
   low-order rejection; loopback stays default and bound security clients fail closed.
2. **Done — pairing and encrypted sessions.** Signed descriptors, recipient-bound encrypted
   invitations, pinned issuer approval, encrypted private credential persistence,
   mutual challenge authentication, replay/tamper/scope rejection.
3. **Done — network repository exchange.** Pinned signed checkpoint, inventory pages,
   bounded object fragments, authenticated imports before metadata references,
   persisted frame progress, post-commit ACK, peer progress and reconnect repair.
4. **Done — native agent.** Runnable script CLI for device/init/invite/enroll/
   serve/sync/watch/status/revoke; explicit addresses and bounded scans/retries. Existing
   review/conflict semantics remain visible instead of overwriting alternatives.
5. **Done with recorded limits — qualification and documentation.** Independent native processes over real sockets,
   offline rename/edit/conflicts, restart/interruption, unauthorized peers and
   hostile frames. Focused tier/GC tests; existing WebSocket gate; one final Lambda
   baseline. Preserve all existing fixture goldens and harnesses.

## Acceptance and limits

- Two processes with separate folders, databases and keys exchange both ways;
  users can run the same commands on separate LAN devices without Internet.
- Unknown/revoked devices, wrong space/recipient, replaced handshake keys,
  replayed messages, wrong sequence and modified ciphertext fail before effects.
- Repeated idle sync creates no revisions; missing objects transfer once;
  process interruption preserves imported immutable content and saved frame state.
- Concurrent offline revisions remain alternatives; moves plus edits converge.
  A failed scan never publishes deletions. ACK never means cloud persistence.
- Current local repository bounds apply. No physical GC, cloud adapter, editor UI,
  browser/ICE/mDNS, automatic key rotation or operating-system key vault in this gate.
- Explicit checkpoint authority is a substantial trust grant: a paired writer may
  attest combined state. Read-only/file-only sharing is not granted by this profile.
- Crypto primitives use [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032.html),
  [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748.html) and
  [HKDF, RFC 5869](https://www.rfc-editor.org/rfc/rfc5869.html).
  The application session protocol is experimental and requires security review
  before public hostile-network deployment.

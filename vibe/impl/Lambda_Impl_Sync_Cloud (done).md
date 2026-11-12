# Durable cloud sync and periodic HTTP fallback

**Date:** 2026-11-11–2026-11-12
**Status:** Native cloud profile and combined peer-listener/polling command qualified; LI-016 resolved.
**Design:** [Data sync §§5, 8, 12.1](../Lambda_Design_Data_Sync.md).
**Spec linkage:** S12.1.1v3 (script policy and procedural IO), D9.3.4
(packages), D11.1.2 and D11.2.1–D11.2.4 (host-controlled IO and grants),
D6.3.3 / S13.1.1v3 (task-owned native IO), S12.4.4v4 (scope transactions).

## Completion record

- Updated 2026-11-12: the cloud profile and combined peer-listener/polling `run`
  command are qualified. **LI-016 is resolved.** The user explicitly authorized
  `lambda-input` changes; the earlier ownership stop no longer applies here.
- Current working checkouts: Lambda `3b8bf560b`, input `903fa51`, with the recorded
  uncommitted cloud and context-list changes. The unrelated merge is complete.
  No staging or commit was performed; no Radiant or vendor source changed.
- Root cause: InputManager releases inputs in creation order, while the previous
  singly linked context list stored them newest-first. Independent detach scanned
  all newer siblings, making shutdown quadratic.
- Owner fix: `lambda-input/lib/mem_context.c` maintains a private previous-sibling
  link. Detach is constant time; cleanup order, allocator callbacks and registry
  locking are unchanged (D5.2.1, D5.2.5). A native mixed-order lifecycle regression
  covers oldest/middle/newest removal, reinsertion and surviving cascades.
- Release-only source reproducer (`-O3 -DNDEBUG -std=c17`): 80k FIFO contexts,
  about 3.03s before and median 0.0097s after. This is an isolated lifecycle
  measurement, not application-throughput evidence. Input `temp/li016/` retains
  before/after source objects, reproducer and JSON receipts.
- Owner focused suites: **87/87** (18 context, 7 factory, 62 pool).
- Final default cloud gate: **17/17 on interpreter, AUTO and JIT**. The combined
  case is mandatory, with the original 180-second deadline and assertions intact.
  Logs: Lambda `temp/li016/final-combined-*.log`.
- Focused every-allocation GC/freed-memory poisoning: **9/9 per tier** on the
  final native host (`final-stress-*.log`). Ordinary sync sweep: **51/51**.
- Full input baseline: **5,220 passed, 3 failed, 1 skipped** (106 executables,
  5,224 tests). Remaining failures are existing LI-001, LI-002 and LI-012; the
  emoji color-font case skips. Input `temp/li016/input-baseline.log`.
- Full Lambda baseline: **7,493/7,494**, all forty suites attempted; native MIR
  GC **368/368**. The sole failure is the previously recorded TeX/expl3 package
  golden discrepancy. No related source, golden or harness was changed.
- Final native host SHA256:
  `fba7ce5d6fd6a6e901862885db6771b8d256fd8424710df5c49ea347356faa87`.
  Lambda `temp/li016/{initial.json,build-inputs.json,final-matrix.json,
  qualification-inputs.json}` record build and final source/executable identities;
  the baseline rebuild supersedes the first follow-up build's host identity.
- Source remains primarily Lambda Script. Only the owner memory-list mechanism
  and its lifecycle test changed natively; the sync protocol/API did not change.
- Remaining scope: LI-015 concurrent administration, hosted/container and physical
  device qualification, finer grants/key lifecycle, traversal and retention.
  The local test store is stopped after qualification. Keep the earlier isolated
  worktree and receipts for historical reproduction.

## Milestones and verification budget

1. **Done — S3 adapter:** SigV4 header/presigned requests, conditional writes, bounded GET/
   HEAD and provider-neutral callbacks. Compare signing with independent vectors
   and exercise real S3 backend conditions; redirects never carry signed credentials.
2. **Done — function:** per-request device signatures, current membership, scoped capabilities,
   immutable receipts, complete publication and CAS heads. All state lives in S3.
3. **Done — cloud agent:** encrypted checkpoint bundles, missing-object uploads/imports, durable
   cloud coverage/cursors, historical publication chain and cloud-only restore.
4. **Done — orchestration:** P2P first, periodic bounded HTTP fallback, retries/backoff,
   separate peer/cloud/install status, CLI and function container entry point.
5. **Done within the qualification limits above — acceptance:** disjoint online times, offline changes, idle deduplication, transport
   switches, rejected authorization, stale CAS, dropped publication response,
   client/function restarts and fresh-device restore with original peers offline.
   Focused fixtures on all execution tiers plus precise-GC checks; final appropriate
   Lambda baseline once inputs are stable. Do not mask existing failures.

## Boundaries

First deployment profile is native full-space writers with explicit checkpoint
attestation, as in the LAN peer milestone. Internet traversal, browsers, finer
sharing, automated key recovery/rotation, OS vaults, destructive retention and
independent security audit remain later work. A cloud acknowledgment requires
complete durable references; it never follows merely from a successful HTTP send.
Hosted-cloud and physical-device qualification require their actual environments.


<details><summary>Initial cloud-profile qualification before LI-016 was fixed</summary>

The following checkpoint describes the original copied host and its then-blocked
combined case. The current checkpoint above supersedes its blocker/status and
native-host identity; original failure receipts remain intact.

- Updated 2026-11-12. The bounded native cloud profile is implemented and
  qualified. Additional combined-agent qualification is blocked on input LI-016.
- Worktree `temp/sync-cloud/worktree`, branch `codex/sync-cloud`, starts
  `dc186e3d52`. Main has an unrelated unresolved merge; do not resolve/stage it.
- All policy is Lambda Script. Existing HTTP, crypto and SQLite mechanisms are
  reused; no C++, vendor, input/lib or Radiant source changed.
- Delivered: S3 signing/capabilities, signed owner control, stateless function,
  dependency receipts and CAS heads, encrypted bundles, client pull/publication,
  durable provider positions, historical/named snapshots, polling and P2P switching.
- The Python HTTP/process adapter is exercised locally. Its function-event wrapper
  and Linux runtime-image Docker template are provided; hosted/container execution
  and physical-device qualification are not claimed.
- Qualified backend: local SeaweedFS 4.53, real S3 signing/conditions/checksums.
  Archive SHA256 `152a89d83e66372cc50c25d67e67a51ab295efbf85b88c139ca9965adc369b6f`.
  Official release: https://github.com/seaweedfs/seaweedfs/releases/tag/4.53.
- Final ordinary process gates: **16/16 on interpreter, AUTO and JIT**, including
  multiple polling rounds, disjoint clients, named/history recovery, function-host
  restart, lost committed response, killed-client recovery before publication,
  checksum rejection, P2P switching and revocation.
  Logs: `temp/sync-cloud/accepted-process-*.log`.
- Sync regressions: **51/51 per tier** (`sync-normal-*.log`). Every-allocation
  precise GC/freed-memory poisoning: **9/9 per tier** (`focused-stress-*.log`).
- Broad script baseline: **2,279/2,284** (`script-baseline.log`). Main-checkout
  recheck passes three worktree-dependent cases; VMap NUL and TeX/expl3 mismatches
  reproduce (`main-recheck.log`). Logged in Lambda's issue ledger. No golden,
  baseline harness or unrelated engine was changed. This was not a native rebuild
  or the full forty-suite native baseline.
- Native copied host SHA256
  `13eca632e6740d1d3a5e692fb34c4ca96c14ae5c388917506ccd061d42c1a4e8`;
  source/test/harness identities and results: `qualification-inputs.json`.
  Initial merge/build context: `initial.json`; all implementation and golden-script
  hashes match the frozen inputs used for the tier/GC gates.
- Package structure passes (**877 scripts**); Python compilation, scoped whitespace,
  golden pairs and documentation links pass.
- **LI-016:** optional combined `run` completes its transfer but stalls during
  process-exit InputManager/lib cleanup. Interpreter/AUTO hit the unchanged
  180-second deadline; the already-running JIT attempt was stopped after the
  blocker was identified. Exact cause/ownership remains unconfirmed. Evidence:
  `combined-failure-{interp,auto}.log`, `combined-interrupted-jit.log`,
  `combined-evidence/` and `agent-sample.txt`. `test_cloud.py --combined` retains
  the failed gate. Stop that affected work; no teardown bypass or sibling fix.
- **LI-015:** simultaneous administration remains unqualified. Permanent retention
  and explicit full-space checkpoint authority remain this profile's boundaries.
- Exported 35 owned paths and the append-only baseline observation to main; its
  index/merge staging is unchanged (`export-index.json`). The three new cloud
  fixtures pass from main too (`main-cloud-recheck.log`).
- Final pre-head crash qualification passes on all three tiers: the test kills
  its owned client after immutable uploads, proves unpublished files remain
  undiscoverable and pending, then resumes with the original operation frontier.
  Earlier 15-case receipts remain in `accepted-fifteen-*.log`; final 16-case gates
  are `accepted-process-*.log`.
- Remaining work: owner investigation of LI-016, hosted/container and physical
  device qualification, finer grants/key lifecycle, traversal and retention.
  No staging or commit was performed. Owned local test services are stopped after
  qualification; this worktree and receipts remain for reproducibility.

</details>

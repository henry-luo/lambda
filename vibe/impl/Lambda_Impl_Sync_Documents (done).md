# Durable live-document sync and headless editor host

**Date:** 2026-11-12
**Status:** Complete for the bounded native/headless profile. The large
workflow every-allocation-GC profile remains unqualified.
**Design:** [Data sync §§7, 12.2](../Lambda_Design_Data_Sync.md),
[CRDT protocol](../Lambda_Design_CRDT_Protocol.md).
**Spec linkage:** S12.1.1v3 (pure policy/procedural IO), S12.4.4v4
(transaction scopes), D9.3.4 (packages), D11.1.2 / D11.2.1–D11.2.4
(host IO and grants), D9.1.1v4 / D9.1.3v2 (project ownership).

## Completion record

- Delivered encrypted persistent Text/Mark sessions, deterministic file/document
  identity, fresh writer actors and editor request receipts. Unsaved edits commit
  before reply and recover after reopening without becoming saved file versions.
- Shared immutable document objects contain compact live checkpoints and retained
  saved endpoints; private state keeps disk bases, reviews, requests and save
  intents. Text/JSON/XML/Mark conversion happens at open/save, with checked native
  Mark element export and a parse-back guard against lossy conversion.
- Existing authenticated peers now negotiate bounded document catalogs and
  exchange atomic deltas or compact checkpoints. Device signatures attest relay
  under the existing full-space writer/checkpoint grant. Cloud bundle v2 binds
  document roots into immutable encrypted publications and dependency receipts;
  periodic polling and the stateless function/storage API stay intact.
- Intent/install/finalize saves capture one exact frontier, publish one linked
  workspace revision and preserve later unsaved edits. Guarded disk readback and
  persisted intent recovery cover a client killed after installation. Retrying
  an edit/save request ID cannot repeat the logical operation.
- External captures fork from the disk base; known saved exports reuse their IDs.
  Unsaved edits survive merging, invalid/overlapping inputs remain reviewable,
  namespace identity review prevents guessed document imports, and deletion keeps
  recovery work. Mark alternatives and explicit resolution are exposed to editors.
- Attachment bytes must reconstruct before document acceptance/ACK. Same-epoch
  compaction drops covered packets while preserving saved endpoints, causal
  anchors, unresolved input and future reconciliation with offline edits.
  Restoring a saved version creates new edits; physical history stays permanent.
- Local, peer and cloud status require the current document root as well as its
  causal frontier. Agent-level cloud status also includes document roots; a
  configured status checks the selected provider. Empty documents, new save
  metadata and doc-only edits cannot inherit a workspace-only cloud acknowledgment.
- The implementation is Lambda Script. This phase changes no native, vendor,
  lambda-input or Radiant source. Existing cloud-phase, LI-016 and unrelated LaTeX
  worktree changes are preserved. Nothing was staged or committed.

## Verification and identities

- Final normal sync gate: **57/57 on interpreter, AUTO and JIT**, ordinary
  60-second fixture deadlines unchanged.
- Focused every-allocation GC and freed-memory poisoning: **5/5 per tier**,
  unchanged exact goldens, complete batch markers and normal process exit.
  Selected cases cover native Mark export, cloud checkpoint dependencies, actual
  unsaved document reopen/save, saved checkpoint links and peer object admission.
- Real document process gate: **18/18 per tier**. Independent clients cover
  retry IDs, offline Text/Mark convergence, incremental peer transfer, concurrent
  Mark alternatives/resolution, compact recovery, saved/unsaved separation,
  external imports, HTTP fallback, attachment recovery, aggregate cloud status,
  multiple polls, retained history, idle deduplication, dropped commit responses,
  a killed save client and return to direct exchange.
- Existing process regressions on AUTO: **12/12 peer**, **17/17 cloud**, including
  the combined listener/polling owner and revocation. Final process gates ran at
  most three at a time with disjoint directories, devices, ports and cloud spaces.
- Milestone aggregate: `make test-lambda-baseline`, **7,507/7,507**, all forty
  suites; script cases **2,395/2,395**, native MIR GC **368/368**. That milestone
  precedes the last focused coverage/GC fixtures and agent status correction;
  the final scoped gates above qualify those inputs. Unrelated LaTeX work
  continued concurrently; no claim is made that this phase authored its fixes.
- Recorded heads: Lambda `3b8bf560b4fd353485d931da969631b76cea5043`,
  input `903fa51773a8e8fca1755198fd9aa5071bbf0cf3`, plus recorded uncommitted
  inputs. Final native host SHA256:
  `50c31d9ed3062d6b270b5e361acc202b5d6c1a6df568658ae0588b64d00a1d92`.
- `temp/sync-documents/qualification-{inputs,results}.json` records **163** input
  hashes and all eleven final gates; its final assertion confirms source/native
  identities stayed unchanged. `final-{sync,stress,documents}-*.log`,
  `final-peer-auto.log`, `final-cloud-auto.log`, `completion.json` and the copied
  `baseline-*.json` retain detailed receipts. Earlier intermediate/source-review
  attempts remain under `initial-60s-*`, `large-300s-*` and
  `before-agent-coverage-*`; they do not supersede the final qualification.
- Package structure, new executable goldens, Python compilation, document links
  and whitespace checks pass. No Radiant baseline is required for these script
  and host changes. No hosted deployment or physical-device claim is made.
- The owned pinned SeaweedFS 4.53 test store (PID 41754) is stopped after checking
  its exact binary/data-directory ownership. `cleanup.json` records its normal
  SIGTERM shutdown; the store launcher and final matrix both exit 0. Child
  clients, listeners, HTTP hosts and proxies are reaped by their test finalizers.

## Unqualified stress case and runtime issue

The large admission workflow exceeds both the ordinary 60-second deadline under
forced GC and the existing 300-second stress budget. Its full-workflow stress
profile is **unqualified**. The native runtime/harness and ordinary deadline were
not changed. `test/sync_documents/test_stress.py --full` retains the attempt;
the finite five-case stress gate is reported separately from the full normal
workflow gates. Do not turn the timeout into a passing result or keep increasing
its budget. A brief collector sample does not establish the precise cost cause.

The alarm-abandon path additionally leaves an interrupted SQLite scope observable
by the next batch item. A one-second CPU-loop reduction followed by a new SQLite
scope produces 124/1; the control alone succeeds. Evidence is recorded under
[LR01-24](../Lambda_Issue_Ledger.md#lr01-24), which remains OPEN. This does not
establish the root cause of its earlier segmentation-fault report. Pending native
cancellation/cleanup work is outside this document-sync milestone. The production
kill/restart recovery cases above use fresh processes and complete successfully.

## Milestones

1. **Done — durable sessions/API:** encrypted head, fresh actors, Text/Mark editing,
   request replay protection, attachments, saved endpoints and restart.
2. **Done — replication:** authenticated deltas, compact recovery, immutable cloud
   roots, provider-aware coverage and periodic fallback.
3. **Done — save/external integration:** exact-frontier linked saves, interrupted
   installation recovery, base-aware imports, notices and review/resolution APIs.
4. **Done — same-epoch compaction:** saved endpoint recovery and reconciliation of
   an offline peer's pending edits without retaining detailed packet replay.
5. **Done within the limits above — qualification/documentation:** headless process
   acceptance, normal all-tier fixtures, focused GC, existing process regressions
   and [API/usage guide](../../lmd/package/sync/DOCUMENTS.md).

## Boundaries

One native owner dispatches editor, peer and polling work sequentially; LI-015
concurrent administrative contention remains outside qualification. Existing
full-space writer/checkpoint grants apply. Source editing and structured editing
are distinct persisted profiles. JSON/XML saves reject unrepresentable Mark
values/names; typed attachment descriptors round-trip through Mark. Array/LCS
reference algorithms and bounded envelopes are not a production performance claim.

GUI/browser integration, physical/hosted-provider qualification, traversal, finer
sharing/key lifecycle, paged large histories, epoch retirement and destructive
retention remain subsequent gates. Objects and saved versions stay permanently
retained in this profile. UI implementation belongs to its owning project.

# Lambda Script CRDT protocol

**Date:** 2026-11-11
**Status:** Complete; experimental document protocol v1 implemented and qualified.
**Design:** [Data sync](../Lambda_Design_Data_Sync.md), §§7 and 12.2.
**Spec linkage:** S12.1.1v3 (pure algorithms), S2.3.1v2 (map order),
S2.6.1v2–S2.6.5 (element projection), D9.3.4 (packages), D11.2 (host grants).
No language semantics changed. The native logical-join fix enforces S3.4.

## Current checkpoint

- Completed in `/Users/henryluo/Projects/lambda`, branch `master`, on 2026-11-11.
  Starting commit: `21fdc7cf6bffa8828a7b6749d97d8e1e0b3b020a`; candidate changes
  remain uncommitted for review. The initial worktree was clean.
- Delivered fourteen pure modules, twenty CRDT fixtures, one native compiler
  regression, the versioned protocol contract and a runnable protocol example.
- Native fix: logical joins preserve actual outcome carriers before selecting
  a native Boolean lane (S3.4). No language ruling or sibling source changed.
- Normal verification: 21/21 affected fixtures on interpreter, AUTO and JIT.
- Stress verification: 21/21 on each tier with collection every allocation and
  freed-memory poisoning, using isolated `test-batch --timeout=300` processes.
  The final S2.2.4 zero-length blob change repeats its affected fixture on all
  three stress modes and repeats all 21 normal fixtures on all three tiers.
- Full Lambda baseline: **7,423/7,423 passed**, including 2,312 script-suite
  tests and the 368-case native MIR GC-stress gate. Debug host rebuilt by `make`.
- Package structure: 848 scripts passed; UI boundaries: 37 entries/43 dependencies.
  Documentation links/formal IDs, fixture companions and owned whitespace pass.
- Separate open issue LR01-24: the default 60s exhaustive stress run timed out
  and then crashed during batch cleanup. Longer-budget unchanged fixtures pass;
  this does not fix or close the timeout-path issue. Original failure is retained.
- Concurrent TeX package/test work and pre-existing ledger edits are preserved.
  The initial ledger update added LR01-24/count changes. The 2026-11-11
  follow-up also registered the resolved logical-join defect as
  [LR07-66](<../Lambda_Issue_Ledger (fixed).md#lr07-66>).
- No document-protocol implementation work remains for this goal. Actual editor,
  filesystem, network, object-store/encryption adapters, browser hosting and
  arbitrary subtree moves remain separate integration/milestone scope.
- Evidence and input hashes: `temp/sync-protocol/`, especially `baseline.log`,
  `<tier>.log`, `<tier>-stress-summary.log`, `empty-blob-<tier>-stress.log` and
  `qualified-inputs.json`. No production performance claim is made.

## Milestones and acceptance

1. **Done.** Strict typed encoding and structural/causal validation. Reject malformed
   operations, unobserved references, counter forks and corrupt checkpoints;
   preserve scalar types and map order without executing document data.
2. **Done.** Versioned HAVE/header negotiation, bounded UPDATE, CHECKPOINT_REQUIRED and checkpoint
   messages, acknowledgments, retries, and restartable pure endpoint state.
   Qualify loss/reordering, pagination, transport switching and compaction races.
3. **Done.** Complete editing/session protocol surfaces: stable positions, ordered Mark
   edits, atomic edit batches, explicit conflict resolution, blob references,
   exact-frontier save preparation/commit and parent-linked saved history.
4. **Done.** Verified persistence bundles, local/peer/cloud acknowledgment distinctions,
   checkpoint trust boundary, schema/profile mismatch and epoch recovery without
   losing pending work. Qualify resumed edits after restore/compaction.
5. **Done.** Update package/design documentation and execute the finite verification gate.

## Scope boundary

The document protocol owns causal convergence, serialization, negotiation and
recovery. Directory move reconciliation is a separate protocol (§6 of the design).
Document adapters own external format fidelity; the host owns authentication,
durable atomic writes and blob transfer. A protocol acknowledgment may represent
only the durability evidence explicitly supplied by the host.

## Delivered implementation

- Fourteen pure modules under `lmd/package/sync`; the original five were
  extended and nine protocol/codec/auth/editing/persistence helpers were added.
- Twenty CRDT golden fixtures, including twelve new fixtures, plus the native
  compiler regression `logical_not_closed_call.ls/.txt`. No native test harness
  registration or expected-output relaxation was needed.
- [Protocol contract](../Lambda_Design_CRDT_Protocol.md), expanded
  [package API/example](../../lmd/package/sync/README.md), and main sync/package
  documentation updates. The protocol is experimental v1, not a deployed service.
- One native source change, `lambda/runtime/mvp/mvp_mir.cpp`: logical value
  joins use the actual operand outcome contracts before choosing a native lane.
  No sibling implementation, vendored source, build configuration or formal
  language ruling was changed.

## Compiler investigation

A reduced `false or not natural(record.total)^ or false` first exposed an
Item-to-native-Boolean rejection under eager JIT. An attempted unary-not-only
fix addressed that case but a transaction-validation variant exposed the common
logical-join cause. The final change removes that special case and joins actual
operand outcomes before choosing a native lane. This preserves Error, absence,
selected values and skipped operands under S3.4. The regression includes both
call and comprehension variants, ordinary errors and short-circuit cases.
LLDB trace/reductions remain under the evidence directory.

## Verification evidence

- Focused normal harness: 21/21 on interpreter, AUTO and JIT. Logs:
  `temp/sync-protocol/{interp,auto,jit}.log`.
- Every-allocation GC plus freed-memory poisoning: unchanged fixtures in
  isolated `test-batch --timeout=300` processes. Complete start/end markers,
  exit code and golden output are checked, including process teardown. Logs:
  `<tier>-stress-summary.log` and `<tier>-stress-300/*.stdout` / `*.stderr`.
- The final zero-length blob change repeats the affected stress fixture on all
  tiers; evidence: `empty-blob-<tier>-stress.log`. Earlier normal results were
  superseded and all 21 normal fixtures rerun.
- The ordinary 60s stress-harness timeout and subsequent cleanup crash remain
  visible in `interp-forced-gc.log`; LR01-24 tracks that separate runtime issue.
  Neither the harness nor the exhaustive convergence fixture was weakened.
- Full baseline: `TMPDIR="$PWD/temp/sync-protocol" make test-lambda-baseline`
  passed 7,423/7,423. Build/test output is `baseline.log`; runner summaries also
  remain under `test_output/`.
- Structural checks: package structure, UI boundaries, local documentation
  links/formal IDs, source whitespace and all fixture golden companions.
  The broader tree's unrelated TeX whitespace is not altered by this task.
- Input identity: current commit plus uncommitted candidate; source hashes in
  `temp/sync-protocol/qualified-inputs.json`. Temporary diagnostic scripts,
  signature-vector generation, debugger output and build logs remain there.

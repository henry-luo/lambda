# Lambda Script replication prototype

**Date:** 2026-11-11
**Status:** Complete and verified as a finite experimental prototype; not a production protocol.
**Design:** [Data sync proposal](../Lambda_Design_Data_Sync.md), especially §§7.7–7.8.
**Spec linkage:** S12.1.1v3 (pure core), S2.3.1v2 and S2.6.1v2–S2.6.5
(Mark projection), D1.7 (source distribution), D9.3.4 (package imports).
No formal language ruling changes.

## Current checkpoint

- Workspace: `/Users/henryluo/Projects/lambda`, branch `master`.
- Starting commit: `8cf23501bf53e28fe8bd233c9102715addd148e1`.
- User authorized the replication prototype after the design review.
- Scope: pure Lambda Script Text and bounded Mark replication, duplicate/reordered
  delivery, compact checkpoints, saved snapshots, external-import branches and
  an older offline replica returning after compaction.
- Excludes transport, filesystem watching, production crypto/storage, directory
  moves, rich-text formatting intent, arbitrary tree reparenting and epoch retirement.
- Baseline: RGA insertion tree with deletion anchors; observed-remove causal
  multi-value registers for independently editable fields. Algorithms are
  prototype choices; no upstream wire compatibility or transferred proof claim.
- The baseline rebuilt the debug host. No C/C++ runtime or sibling source edits.
- Unrelated issue-ledger and TeX work is present and must remain untouched.
- Implemented `lambda.sync.core`, `.sequence`, `.text`, `.mark`, `.session`.
- Eight goldens pass under interpreter, AUTO and JIT, including 120 delivery
  permutations and 225 text replacement pairs. These qualify the current code.
- Full Lambda baseline passed 7,408/7,408 before the final shared-lookup extraction;
  the eight affected fixtures were rerun on every tier after that extraction.
- Final normal and forced-GC/poison checks passed after helper extraction:
  eight fixtures on three tiers in both modes, 48 passing fixture executions.
- Package structure, UI boundaries, document links/formal IDs and source/golden
  whitespace checks passed. No required prototype work remains.

## Finite milestones

1. **Done.** Replication kernel and Text: immutable states, actor counters, causal receive
   buffering, deterministic RGA order, multi-value fields, state join and compact
   checkpoint recovery. Reject incompatible documents/epochs and actor forks.
2. **Done.** Mark-oriented model: stable map/array/element/text nodes, scalar fields,
   ordered children and explicit conflict projection. Preserve established IDs.
3. **Done.** Session/history bridge: base/live/external separation, independently identified
   external branches, idempotent imports, saved versions and retained endpoints.
   Preserve dirty edits and conflict inputs; unknown bases require review.
4. **Done.** Qualify reordered/duplicate delivery, merge algebra, deleted anchors, external
   overlap, repeated imports, post-compaction offline edits and saved restore.

## Algorithm references

- [Gomes et al., Verifying Strong Eventual Consistency, 2017](https://arxiv.org/abs/1707.01747):
  RGA insertion/deletion and retained tombstones; [reference](https://github.com/trvedata/crdt-isabelle).
- [Almeida et al., Delta State Replicated Data Types, v1](https://arxiv.org/abs/1603.01529v1):
  dot context and multi-value register joins; [authors' implementation](https://github.com/CBaquero/delta-enabled-crdts).
- Shared core composes immutable RGA atoms/deletion sets and causal field entries.
  Compaction discards covered packets and deleted payloads while preserving
  IDs, anchors, causal coverage and integrity receipts. It does not purge anchors.

## Delivered artifacts and qualification

- Source/API: [sync package](../../lmd/package/sync/README.md), five pure modules.
- Runnable example: [sync_example.ls](../../test/lambda/sync_example.ls).
- Eight fixtures in `test/lambda/sync_*.ls`, each with a `.txt` golden, discovered
  by the unchanged Lambda GTest harness. No test registration/build edit needed.
- Core: causal buffering, dot-register joins, RGA ordering, clone/counter-fork
  rejection, compaction floors and state fallback for an old peer.
- Model: native maps/arrays/text/scalars and an element inspection record;
  map/attribute order at construction and normalized element content projection.
- Session: independent external actors, raw/parsed input retention, non-unique
  LCS correspondence detection, conflict/dirty state, immutable saved endpoints,
  logical thinning and compacted import-idempotence receipts.
- Full baseline command: `make test-lambda-baseline` — 7,408 passed. Initial
  sandbox write denial for sibling build outputs was resolved by the authorized
  elevated build; no source defect or vendor modification was involved.
- Focused command: `LAMBDA_EXEC_BACKEND=<interp|auto|jit>` with
  `test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/sync_*'`.
- Stress adds `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
- Final focused result: 48/48 fixture executions passed with the current package.
- Package structure: 834 scripts passed. UI boundaries: 37 entries / 43 dependencies.
- Document/source checks: 13 local links/anchors and 21 formal IDs resolved;
  balanced fences, clean whitespace and all eight fixture goldens present.
- Evidence: `temp/sync-prototype/baseline.log`, `<tier>.log`, `<tier>-forced-gc.log`;
  full-run summaries are also in `test_output/test_summary.json`.

## Limits and next integration work

The [package README](../../lmd/package/sync/README.md) defines the finite API and
remaining limits. In particular, snapshots are trusted in-memory models;
fingerprints are not signatures, RGA anchors/receipts can grow, and unreachable
structured nodes are not garbage-collected. No protocol compatibility, formal
proof, minimum encoding, native-element export or performance result is claimed.
Production Mark moves/schema, external-format fidelity, durable actor allocation,
host races, transport, key lifecycle, object storage and maintenance remain gates
in the proposal. This prototype does not change the actual editor save/reload UI.

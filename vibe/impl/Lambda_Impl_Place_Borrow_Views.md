# Place borrows and row snapshots

## Current checkpoint

- Goal: implement the user-approved LR12-10/LR12-14 clarifications and
  requalify LR01-17 before archiving resolved records.
- Workspace: `/Users/henryluo/Projects/lambda`, branch `master`, base
  `6b68618a2`; updated 2026-11-03.
- Settled: S9.1.2/S9.1.3 require snapshots and inout publication. Place
  arguments use caller-owned precisely rooted homes, never pointers into
  movable container buffers (D4.3.1/D4.4.4v4). Writable scalar places are
  admitted under invariance/exclusivity. Partial writes publish on errors.
- Settled: ordinary matrix rows are snapshot values; write-through views
  are confined to non-escaping var-argument positions (S9.2.2). The user
  lifted CW32v2 item 6's migration deferral on 2026-11-03.
- Async: D8.1.1v18 already requires T0 task execution; representative
  `conc/start_wait` passed interp/auto/jit before implementation.
- Preserve concurrent MVP, RDB and documentation work. Formal Semantics
  and its generated index already have concurrent changes; merge edits.
- Rulings recorded: S9.1.3v3, S9.2.2v2 and D4.4.7; CW32v3/CW33v2
  replace the mutable-view deferral and movable interior-pointer mechanism.
- Implemented: both tiers transport caller-rooted place homes and evaluated
  path keys, publish after calls/error results, and support scalar leaves.
  A home is isolated from its place so plain ancestor arguments snapshot.
- Numeric views: ordinary row reads materialize the selected value; public
  view constructors retain view descriptors over private source snapshots.
  COW clones materialize views with exact lanes and logical dimensions.
- Focused new record/array/scalar/nullable/import/key-once/error fixtures pass
  interp/auto/jit. Earlier 378-run sweep is superseded: it exposed an
  interpreter reborrow evaluation regression, now repaired, plus metadata
  expectations and the pre-existing strict-T0 SYS_FUNC exclusion.
- Current verification: debug `make build` passes; forced-GC sweeps and
  concurrency requalification next, then the required Lambda baseline.
- Next action: finish focused checks, update affected docs/ledger, and run
  `make test-lambda-baseline` with listener reruns if sandbox denies sockets.

## Acceptance

Place arguments preserve caller writes, rebinds, scalar updates and prior
snapshots on both tiers, including recoverable error exits and moving-data
GC. Matrix-row value boundaries preserve snapshots in both directions;
nested matrix writes and explicit procedural borrows still update the owner.

## Verification

Pending.

# Radiant Full-UA Editable — UA-6 Release and Verification Record

**Date:** 2026-09-07  
**Status:** applicable D7.2.5 UA editing acceptance complete

This is the execution record for
[Radiant_Impl_Editable2.md](Radiant_Impl_Editable2.md). It records the
implemented package-owned `contenteditable`/`designMode` behavior required by
D7.2.5 and the verification boundary used for its completion.

## Implemented boundary

`lambda/package/dom` owns descriptors, context validation, planning, command
and query dispatch, normalization, history, and default editing behavior.
Native Radiant provides only platform input, checked generic DOM mutation,
Selection/Range, clipboard transport, observation, and precise-rooted package
crossings. The source ownership and no-emulation audits are clean.

The separately proposed unified form-control/history migration in
`Radiant_Design_Edit_History.md` remains out of scope. Existing form editing
and history retain their owner and passed their regression suite. This follows
D7.2.5; native crossing ownership follows D5.3.3.

The Chromium reference is a linked worktree whose parent may advance for
unrelated reference pages. Manifest verification therefore compares the
`editing/` subtree against the pinned revision as well as checking every
selected-file hash and local modification. An unrelated parent `HEAD` change
cannot waive, replace, or alter the pinned corpus.

## Accepted verification

The following commands completed successfully on this implementation:

- `make test-editable-ua` — package-disabled behavior, editable UI fixtures,
  all 31 CodeMirror/Editor.js/ProseMirror integrations, legacy form editing,
  3/3 applicable WPT contenteditable cases, 313/313 input-event assertions,
  and 4/4 pinned Chromium cases.
- `make test-radiant-baseline` — pass; layout baseline reported 7,410 passes,
  361 partials, 0 failures, and 6 skips, with DOM/UI/input-event regression
  groups green.
- `make test-lambda-baseline` — pass; 2,104 input plus 2,021 runtime cases
  (4,125 total).
- `make lint` and `make lint ARGS='--rule ^no-int-cast-radiant$'` — pass.
  The GC-effect audit verified 49 `NO_GC` imports and 94 project call-graph
  nodes.
- `make release` — pass. Release probes for structural undo/redo, rich
  clipboard, and selection-wrapper GC lifetime passed. Ten independent
  selection-wrapper lifetime runs completed without a crash.

Five process-level release runs of the structural history fixture took 0.25,
0.20, 0.21, 0.22, and 0.21 seconds (median 0.21 seconds; observed maximum
0.25 seconds). This is an end-to-end launch/layout fixture measurement, not a
claim about individual edit-operation latency.

## Repository-wide aggregate attempt

An additional `make test` aggregate run was attempted. It was not accepted as
an editable conformance result: before the relevant contenteditable (3/3) and
Chromium (4/4) groups passed, unrelated extended suites reported existing
failures (including view reuse, CSS animation/syntax, clipboard, HTML
round-trip, PDF visual, validator, HTTP, general selection, DOM events, form,
and CSSOM-view groups). The general WPT DOM-nodes child then produced no output
for 180 seconds and was killed by the runner; the parent was interrupted and
ended with exit 130.

This report deliberately does not call that aggregate run green. Its failures
are outside the D7.2.5 selected editable corpus and did not reproduce in the
Lambda or Radiant baselines above. They remain repository-wide test debt to be
resolved independently rather than hidden by an editing exclusion.

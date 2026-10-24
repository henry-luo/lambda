# View-state retirement cleanup

## Milestone

Closing Word dialogs must leave the interaction registry valid in release builds.
Detached live controls retain their values; retiring their native identities
releases their state without requiring another layout pass. This follows the
generation-checked ownership boundary in **D4.5.1v4** and the retained presentation
policy in **D4.5.3v2**.

## Cause and repair

The Word ribbon-appearance workflow reproduced 25 occurrences of
`view state registry id has no live view`. Diagnostic tracing identified retired
form-control owners from closed dialogs. Reflow pruning intentionally preserves
registered detached controls, since a script may still hold and reattach them.
Retirement later invalidated those identities without releasing their ViewState
entries. Debug cascade pruning hid the missing cleanup; release validation kept
reporting it on subsequent events.

`dom_retire_collect` now invokes the existing `view_state_detach_subtree` after
the subtree's retirement pins are clear and before native property teardown and
identity retirement. The same bridge works for documents without a layout tree;
DOM-only targets retain the existing weak-host-hook pattern. No validation or
logging checks were relaxed.

The existing detached-control lifetime regression now requires the state entry
to disappear at retirement itself. It first failed against the old implementation
and now passes, including the earlier detach/reattach value checks. A subsequent
reflow prune has no leftover entry to remove.

## Verification

- State-store suite: 29/29 passed.
- The reproduced Word ribbon workflow: zero interaction-state errors after the
  fix, compared with 25 before it.
- AddressSanitizer: the interpreted Word ribbon workflow passed all 26 checks,
  with no invalid-memory or interaction-state reports (`detect_leaks=0`). The
  automatic-tier attempt stopped before the document opened with a separate
  background compiler use-after-free: `script_compilation_unit_id`, while an AST
  prebuild worker freed a script used by satellite compilation. This remains an
  independent startup qualification limit; its trace is `asan-ribbon.stderr`.
- Radiant baseline: passed with 6,147 passing cases, 141 existing partials,
  two skips and no failures, including all 661 UI automation cases.
- Word native/export workflows: 81/81 passed with error logging enabled;
  every workflow's stderr was checked for the reported interaction-state error,
  with no occurrences. Artifacts: `temp/word/state-registry/`.

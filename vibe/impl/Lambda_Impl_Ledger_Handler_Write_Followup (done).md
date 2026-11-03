# Ledger handler values and invalid writes follow-up

**Status:** selected implementation and qualification complete.
**Date:** 2026-11-03.
**Base:** `09f81fdfb`.

## Scope and authority

This pass applies existing **S7.1.3v3 / S7.7.6v2** store-failure rules and
**S7.6.1v9 / S7.6.7v5** procedure-handler result rules. **LR02-35** (unresolved
names), **LR09-31** (formatter size policy), and the **LR12-27** question about
discarding a plain procedure call's write error at its caller remain pending
rulings. No semantics changed.

## Findings and repair

**LR10-13 / LR10-18 were already repaired by the 2026-11-02 M1 handler work.**
The original named-procedure example produces `[2, -1]` on interpreter, JIT and
automatic execution. Existing `proc/handler_results.ls` covers normal/error
results, explicit return, fresh recovery errors and suspension. It now also
checks the string and map return types from LR10-18. Both stale entries moved
to the fixed ledger.

**LR12-27 still reproduced for a direct unannotated local write.**
`pn main() { var b = [1]; b[5] = 2; print("unreachable") }` printed the marker
and exited zero on all three tiers at the base. `fn_array_set` already returned
a rich E302; the remaining defect was dropping that completion before a store
boundary consumed it.

- T0's `exec_place_assign` established the binding's containment/reporting
  boundary only for explicitly typed map/array roots. It now applies that same
  boundary to rejected unannotated stores.
- MIR's raw positional fallback arms returned setter status to statement
  lowering, where a procedure without a declared error return discarded it.
  `emit_array_num_store_fallback` and `emit_compound_array_store_fallback` now
  route a rejected setter through the existing binding boundary at the cold
  call site. The runtime-dispatched int-store arms reuse the latter helper.
  Successful direct stores retain their existing hot paths.
- Existing **D6.1.3v2** defect transport carries the completion through a native
  result without reinterpreting its bits as a number. This pass changes the
  originating store boundary, not caller policy for a discarded plain call.

The original main now reports E302 and exits one without running its tail.
`proc/ledger_index_write_error.ls` checks negative/high indices, typed and
unannotated owners, int/float call results, retained COW snapshots, unreachable
tails, recovery, subsequent valid writes and local-block containment. The
negative original is `negative/runtime/index_write_out_of_bounds.ls`; its
native test requires exit one, empty stdout and E302 on every tier.

## Qualification

- **56/56** focused executions: six positive fixtures and the negative main
  on interpreter, JIT, automatic execution and automatic execution with
  promotion threshold one, each with normal and every-allocation/poisoned GC.
  Strict interpreter runs report zero fallback/exclusion.
- Focused native regression checks **3/3**.
- Required `make test-lambda-baseline`: **8,700/8,708** initially passed.
  Eight listener cases failed under the filesystem/network sandbox; the server
  failure explicitly reported `operation not permitted` on loopback bind.
  All eight pass with local socket access: server suite **152/152**, affected
  script goldens **4/4**. Every baseline case therefore passes across the
  initial run and these environment-corrected reruns.
- MIR emission **285/285**, instruction ratchet **20/20**, MIR GC stress
  **355/355**, runtime diagnostics **324/324**, input parsers **2,112/2,112**.
- `git diff --check` passes.

Evidence: `temp/ruled-ledger-build.log`, `temp/ruled-ledger-focused.json`,
`temp/ruled-ledger-focused-gtest.{log,json}`,
`temp/ruled-ledger-baseline.log`, and the separate listener rerun records
`temp/ruled-ledger-serve.{log,json}` and
`temp/ruled-ledger-socket-scripts.{log,json}`.

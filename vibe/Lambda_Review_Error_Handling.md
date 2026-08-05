# Review — Lambda Error Handling Implementation Plan

**Reviewed proposal:** [`Lambda_Impl_Error_Handling.md`](./Lambda_Impl_Error_Handling.md)  
**Review date:** 2026-08-05  
**Conclusion:** The proposal has a strong semantic direction—especially the native-lane
invariant—but it is not ready to implement as written. Several sequencing and runtime issues
could cause regressions or make the intermediate phases impossible to land green.

## Blocking issues

### 1. The phase order contradicts the test gates

Phase A removes `let value^err`, while corpus migration is postponed to Phase E. Yet the
proposal says A–B are independently landable and every phase must pass the baseline
([proposal §3](./Lambda_Impl_Error_Handling.md#L58),
[A–B landing claim](./Lambda_Impl_Error_Handling.md#L68),
[Phase E](./Lambda_Impl_Error_Handling.md#L193)).

A live scan on 2026-08-05 found at least 245 `let name^err` occurrences across 119 `.ls`
files—not approximately 81 files. Removing the grammar first would immediately break the tree.

**Recommendation:** Add the new handler while retaining the legacy syntax temporarily.
Implement and validate it, migrate the corpus, then remove the old syntax and update E228 in
one atomic phase.

### 2. Removing `^err` currently removes system-fault recovery

The old destructuring emitter is not merely syntactic sugar. For eligible procedural
expressions it installs a `LambdaRecoveryFrame` with a native `setjmp` landing point
([`transpile_local_fault_expression`](../lambda/runtime/transpile-mir.cpp#L7896),
[`transpile_error_destructure`](../lambda/runtime/transpile-mir.cpp#L7998)).
`AstNamedNode.local_fault_safe` also prevents that frame from spanning an async suspension
([`AstNamedNode`](../lambda/runtime/ast-core.hpp#L349)).

Therefore, the proposal's claim that the new handler can initially lower to today's ordinary
contagion semantics is incomplete. That catches returned `ItemError` values, but not native
system/resource faults.

Before retiring the old form, specify:

- How `e ^ { ... }` installs or reuses `LambdaRecoveryFrame`.
- Whether a handler may protect an expression containing `await`.
- How fault recovery is represented across task polls if allowed.
- How the frame is retired on normal exit, handler entry, `return`, `raise`, and `continue`.

Also soften C1's “there is no dynamic unwinding” statement: TE-15 routing may be static, but
native fault recovery demonstrably uses non-local recovery frames.

### 3. Three different error regimes are being conflated

The design needs explicit distinctions between:

- A soft error returned as an ordinary boxed value.
- An implicit TE-15 defect that skips unsuccessful computation.
- A user-raised `T^E` error requiring E228 acknowledgement.

B4 correctly says a raised error must never enter TE-15 skip machinery, but C2/C3 currently
place all origins and acceptors into one broad routing model.

Use an explicit route kind such as `SOFT_VALUE`, `DEFECT_SKIP`, and `RAISED_ERROR`, with each
destination declaring which kinds it accepts. Otherwise a later emitter change can accidentally
send raised errors to block landing pads.

There is also a direct inconsistency: B4's “exact” E228 set omits `or`, while B5 and the current
validator explicitly recognize `F() or default`
([`validate_enforcing_calls_in_expression`](../lambda/runtime/build_ast.cpp#L9267)).

### 4. Container acceptors must be type-sensitive

C2 says every list/map/element child accepts an error as data. That is only sound for boxed,
untyped, or explicitly error-admitting slots.

For example, an `int[]` element lane or a map field contracted as `int` cannot retain an error
while preserving the native-lane invariant. Its failure must route outward. Define acceptance
from the destination contract, not merely from the fact that the expression is inside a
container.

### 5. The hardest semantic question remains open

The proposal calls itself “decision-complete,” but statement-position defects in `pn` are
explicitly undecided ([scope](./Lambda_Impl_Error_Handling.md#L37),
[open item](./Lambda_Impl_Error_Handling.md#L239)).

“The smallest enclosing block” is not always a safe destination: if that block's result is
discarded, the defect evaporates and an earlier binding may remain visible.

**Recommendation:**

- Track whether a region's result is observed.
- Never land a defect in a discarded-value region.
- Route it to the closest observed/error-preserving destination.
- If none exists, exit through the function's implicit defect channel.

Do not call that “raise-channel escalation” unless the language intentionally changes it into a
user-raised error. That would conflict with the proposal's own raised-error taxonomy and E228
rules.

### 6. Landing pads need a complete cleanup contract

C4 mentions rooting the error and restoring side-stack watermarks, but ordering is critical. If
the error payload or number home belongs to the region being restored, restoring first can
invalidate the value delivered to the handler.

Each destination record should carry at least:

- Landing label and accepted route kinds.
- Result/error home.
- Root and number-stack checkpoints.
- Task-scope depth and required unwinding.
- Recovery-frame cleanup.
- Whether the region's result is observed.

The error must be adopted or rehomed into destination-owned storage before the abandoned region
is restored.

### 7. Effect analysis and ABI need stronger definitions

The current native error lane is populated for native `can_raise` functions
([native result analysis](../lambda/runtime/transpile-mir.cpp#L16517)). TE-15 defects would
require that lane even when the declared type does not raise.

Rather than continuing to overload `may_return_error`, separate:

- `may_defect`: implicit TE-15/system control effect.
- `can_raise`: declared raised-error channel.
- Returned soft-error possibility: derived from the semantic return type.

`may_defect` must be computed as a conservative call-graph fixed point covering recursion,
indirect calls, imports, and unknown callees. Unknown must default to defect-capable.

The invariant at [proposal §5.5](./Lambda_Impl_Error_Handling.md#L236) should say: “forcing every
function to `may_defect = true` must not change observable results.” Clearing the bit is not
safe; it can remove necessary checks.

## Additional refinements

- Define the new handler's exact grammar precedence and associativity. Current postfix
  propagation is attached specifically to `call_expr`, so “purely lexical” is not a complete
  grammar specification.
- Add parse tests for pipes, indexing/member access, nested handlers, `or`, parentheses, and
  `f()^ - 1`.
- Implement `~` through a nested current-value context stack. Its current match/pipe machinery
  is not sufficient for reliable innermost shadowing and precise error typing.
- Define handler typing in terms of normal completion. “Letting an enclosing block skip” is a
  runtime possibility, not automatically a statically diverging expression.
- Move D5's no-materialization fast path into an optional optimization phase. It is valid only
  when `~` is unused and the origin supports a status-only path.
- Replace the universal “happy-path cost is zero” claim. Intra-function retargeting can be free,
  but an effectful cross-function native call adds the load-and-branch described by D3.
- Make Phase C/D depend on the explicit `MirValue` representation contract from the compiling-
  lane proposal, rather than creating a second raw-register provenance mechanism.
- Prefer symbol-based source anchors in the plan. Many of its recorded line numbers have already
  moved in the live tree.

## Suggested phase structure

1. Settle route kinds, statement-position semantics, typed-container acceptance, grammar
   precedence, and async fault behavior.
2. Land behavior-neutral routing, effect-analysis, and recovery infrastructure.
3. Add `^ {}` while retaining the old syntax.
4. Implement full returned-error and system-fault handler behavior; migrate the corpus.
5. Remove `let value^err` and prefix `^`, and update E228 atomically.
6. Enable TE-15 intra- and cross-function routing.
7. Add D5/`or` fast paths only after correctness and differential tests pass.

## Test additions

In addition to the proposal's existing tests, add:

- A handler grammar/precedence matrix covering calls, operators, pipes, members, indices,
  parentheses, and nested handlers.
- Typed-container rejection versus untyped/error-admitting container acceptance.
- System/resource-fault capture by the new handler.
- Explicit async/`await` handler behavior.
- Task-scope cleanup and forced-GC tests proving handler error payloads survive restoration.
- A differential mode that compiles once with normal effect analysis and once with every callee
  conservatively marked `may_defect`, then compares values and observable effects.
- `make test262-baseline` after shared MIR call ABI or `Context.mir_return_lane` changes, in
  addition to `make test-lambda-baseline`.

## Recommended status

Change the proposal status from **“decision-complete”** to **“semantic decisions pending”**
until at least the statement-position and async recovery questions are resolved.

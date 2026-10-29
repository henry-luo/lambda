# Lambda issue-ledger ownership follow-up

**Date:** 2026-10-29. **Status:** complete. **Scope:** LR12-32, LR12-34 and LR13-17, with a fresh
verification of LR12-41. Existing rulings govern every change; no language
policy or pending ruling was changed.

## Reproductions and fixes

### LR12-32 — tail-place borrow write-back

**S9.1.3 / S1.6.** T0's tail-call loop replaced parameter values while keeping
the caller's original var homes. Passing `node.next` therefore published the
deepest leaf over the caller's root. Permuting two var parameters likewise
published each result into the other home.

The planner reuses a frame only when each recursive var argument resolves to
that same parameter binding. It uses the shared argument/borrow resolver, so
named arguments follow the same rule. A place or a different binding takes an
ordinary call and retains its pending write-back. Same-home recursion remains
a loop; the fixture includes 10,000 iterations.

Regression: `test/lambda/proc/ledger_tail_place_borrow.ls`, covering member and
indexed places, named argument order, permuted homes and unchanged homes.
Before the fix, interp and AUTO corrupt the first four cases; JIT agrees with
the expected output.

### LR12-34 — capture the value produced by an insertion

**S9.1.2 / S9.3.1.** The previous pass fixed reassignment from a place. The
remaining JIT store helper captured only a direct binding, so `r.x = m[0]`
retained an unmarked child. Mutating the source changed the stored value.

A scoped store context now captures the RHS at its existing expression
producer boundary, without reevaluating it. The selected value crosses the
shared insertion-capture helper, which records sharing on its possible source
roots. The destination also records shared children; later writes through
either observer rebuild their path. Bare bindings keep their established
pre-store mark, scalar expressions skip capture, and an RMW handle's final
store-back keeps its existing exemption (**D4.4.4v4**).

Regression: `test/lambda/proc/ledger_place_insertion.ls`, covering member and
index stores, conditional and operand-returning RHSs, mutation through both
observers, a side-effecting index evaluated once and the already-fixed
reassignment case. The preserved executable corrupts the JIT insertion cases;
interp and AUTO already produce the expected snapshots.

### LR13-17 — schema loader and imported-type lifetime

**S11.4.1v3 / S16.9.8 / D4.2.6.** The validator's standalone AST builder had no
Runtime. An import reached the ordinary script-cache loader and dereferenced
that missing owner. It also lacked the schema file's resolution directory.

Schema compilation now uses the ordinary loader with an AST-only Runtime.
The existing loader resolves relative imports, finalizes definitions and
retains dependency owners; schema compilation executes no module code. A
source-path-aware API supplies the schema's actual path, and the existing
three-argument C API remains available. Imports that require deferred
cross-language execution fail schema loading with a diagnostic.

Imported Type pointers and report paths can outlive the validator registry.
The result pool therefore owns the AST loader through `pool_add_cleanup`,
rather than releasing it at registry destruction. CLI-created results expose
their owned pool through the existing `validation_result_destroy` API; the
CLI releases it after reading the result and before shutting down the shared
input cache. Caller-owned validator pools retain their existing lifetime.

Qualification caught an intermediate shutdown fault when the CLI's result
pool survived the input cache. LLDB identified `runtime_free_script` during
late context teardown. Explicit result destruction fixes that ownership order;
the failure was not suppressed.

Regressions:

- `test/lambda/validator/schema_ledger_import.ls` plus its public type module
  and valid/invalid JSON inputs exercise a relative import outside the cwd.
- `ValidatorCliImports.RelativeTypesAdmitAndRejectDataOnEveryTier` checks
  success, type rejection and normal CLI shutdown on interp, JIT and AUTO.
- `AstValidatorTest.ImportedSchemaOwnersSurviveRegistryDestruction` checks
  repeated schema loading and error reporting after registry teardown.

### LR12-41 — current behavior verified

**S9.1.2 / S9.1.6.** The original parsed-element computed-write case already
passes on the preserved executable. The new
`test/lambda/proc/ledger_parsed_element_index.ls` pins two constant-key writes,
a variable-key overwrite, preservation of existing child attributes and
independence of the original parsed document. This is verification of an
existing fix, not a new implementation attributed to this pass.

## Qualification

| Gate | Result |
|---|---|
| `make test-lambda-baseline` | **8403/8403**, including 2112 input cases and 6291 runtime cases. |
| Focused debug tests | **15/15** across interp, jit and auto; **15/15** again with forced GC and freed-memory poisoning. |
| Focused release tests | **15/15** normally and **15/15** with forced/poisoned GC, on the same three tiers. |
| AST validator / validator / path reporting | **39 + 44 + 18 passed**; the validator suite retains its 6 skips and 87 disabled tests. |
| CLI import gtest | Passes valid and invalid imported-schema data on all three tiers. |
| MIR emission / size ratchet | **279/279** and **20/20**; no expectation or budget changes. |
| `make test262-baseline` | **40261/40261**, zero regressions; the runner reports 2652 existing skips. Includes its async lane and required release build. |

The first emission review found eight unnecessary scalar-join capture calls
in DeltaBlue. Reusing the shared expression-shape proof excludes those
non-container results. Final per-function instruction counts match the
preserved executable, with only address-bearing adapter names differing;
the complete size gate passes without changing its budgets. No debug timing
was used as performance evidence.

The working evidence is under `temp/ledger-followup-20261029/`: initial
reproductions, the preserved executable, controls, focused debug/release
results, schema shutdown backtraces, emission review, provenance hashes and
suite logs. This directory contains local evidence, not committed fixtures.

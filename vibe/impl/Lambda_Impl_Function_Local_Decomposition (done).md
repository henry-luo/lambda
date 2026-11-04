# Function-local comma declarations and capture escape — follow-up

**Date:** 2026-11-02.
**Status:** complete and qualified in `temp/fn-local-worktree`, branch `codex/fn-local-state`.
**Authority:** S9.1.2, S9.1.4v2, S12.2.1v3, S16.8.7; D5.3.2, D7.2.3.

The earlier parent-fn completion check missed comma-decomposition paths.
`var a, b = [1, 2]` receives immutable binding flags and rejects reassignment
on every tier. More seriously, `let escaped, unused = [read, 0]` disconnects
the reader from escape analysis: a returned reader captures live parent
state and observes a later value rather than its creation-time snapshot.
Both are reproduced in `temp/fn_local_followup/` before the fix.

## Scope and implementation

1. Give ordinary and decomposed `var` targets the same binding permissions.
2. Propagate source closure values to every decomposed target in the shared
   escape graph. Unknown projections conservatively carry the source flow.
3. Use ordinary T0 declaration binding and shared MIR home initialization
   for projected locals, including fresh homes on each lexical iteration.
4. Mark projected containers as independent COW owners and keep source
   children shared; root the source across extraction and home allocation.
5. Preserve immutable `let` and escaping captures, the fn-to-pn ban,
   source-evaluated-once behavior, missing-member nulls and type widening.

No semantic ruling changes, new loop semantics or pn-call exception is
required. General comprehension mutation remains SO51. Other previously
base-controlled corpus/audit failures remain separate.

## Acceptance and verification budget

Use focused golden fixtures for positional/named mutable declarations,
container copies in both directions, widening, loop-local captures,
escaping readers and rejection of escaping writers through decomposition.
Run interp/AUTO/JIT and every-allocation GC/poisoning checks. Build only
required targets during fixes; run the required Lambda/input baseline and
whole interpreter sweep after the implementation stabilizes. Existing
mismatches must stay visible; do not alter their goldens or sweep manifests.
Update formal implementation footnotes, owning design status and the final
record with actual results, then regenerate/check the formal index.

## Qualification

- All 105 retained benchmark source files compile through the production frontend.
- Decomposition and capture fixtures pass interp/AUTO/JIT with forced GC and
  poisoning. New mutable literal and loop-continuation cases also pass.
- The full default sweep covers 1,426 scripts: 1,406 match, 11 excluded, the
  same nine base-controlled mismatches, zero timeouts. All 20 function-local
  fixtures match with zero fallback. The frozen interpreter binary and source
  hashes are in `temp/fn_local_followup/sweep_final_snapshot.json`.
  `build_ast.cpp`, `interp.cpp` and `interp_plan.cpp` still match that snapshot;
  later benchmark fixes changed MIR lowering and receive their final baseline
  and emission qualification separately.
- Precise-root hazard audit passes for 18,561 migrated native functions.
- Documentation checks: functions 31 clean / 3 expected errors; procedural
  reference 26 clean / 4 expected errors / 1 no-run. Formal index is current;
  its duplicate DO25 warning predates this work.
- Final `make test-lambda-baseline`: **8,533/8,533 pass**, including 282 MIR
  emission checks, 20 emission-budget checks and 353 MIR GC stress tests.
  Log: `temp/fn_local_followup/baseline_qualified.log`. Performance qualification
  is recorded separately in [the benchmark plan](<Lambda_Impl_Function_Local_Benchmarks (done).md>).

The nine existing sweep mismatches are `type_param_error_short_circuit.ls`,
`chart/test_calendar_controls.ls`, `chart/test_expression_integration.ls`,
`chart/test_statistical_transforms.ls`, `chart/test_table_transforms.ls`,
`chart/test_transform_controls.ls`, `chart/test_window_lookup.ls`,
`latex/test_latex_phase3_corpus.ls` and `proc/proc_var_reassign_elem.ls`.
Base/current controls are retained in `temp/fn_local_sweep_comparison.json`;
no sweep manifests or expected outputs were changed to conceal them.

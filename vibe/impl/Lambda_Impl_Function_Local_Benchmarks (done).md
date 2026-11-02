# Function-local state: benchmark migration and performance qualification

**Date:** 2026-11-02. **Status:** complete and qualified.
**Authority:** S12.1.2v3 (local control), S9.1.3v2 (value/inout parameters),
S9.1.4v2 (captures), D6.1.2v2 (call colour), D8.1.1v18 (tier promotion).
**Workspace:** `temp/fn-local-worktree`, branch `codex/fn-local-state`.

The benchmark corpus historically uses `pn` for pure algorithms because those
algorithms need loops and working variables. Convert those kernels and pure
helpers to `fn` while retaining their inputs, iteration counts, checksums and
timing boundaries. Keep `pn main()` for clock/print and keep procedures that
require inout parameters, effects, or unsupported comprehension control.

## Implementation

1. Snapshot the original benchmark tree at commit `911a0b1ed8` before editing.
   Audit declarations and local/imported call graphs. A converted function
   must not acquire a procedural callee. Copy a rebound scalar value parameter
   into a local `var`; keep explicit inout parameters procedural. Preserve
   annotation coverage in the existing typed and untyped variants.
2. Compile every changed script through the production compiler. Preserve
   functions requiring a return-contract or control-flow redesign as procedures;
   do not weaken static errors or alter workload goldens.
3. Compare original and migrated sources with archived release binaries.
   Use one binary to isolate source migration, and `--before-binary` to include
   runtime changes in the final before/after comparison. Resolve code generation and promotion regressions exposed by migration.
4. Qualify engine changes with tier/GC checks, MIR emission assertions, the
   Lambda/input baseline and the complete interpreter sweep. Keep preexisting
   mismatches visible. Document actual results and limitations.

`test/benchmark/function_local_migration.json` records converted functions and
retained-procedure reasons. The paired runner follows the canonical benchmark
registry, including typed/untyped paths and changed imported helpers. Changed copies omitted by registry deduplication and standalone timing probes
receive their own paired rows. Imported helper modules are covered by their
callers; all changed files also receive production compilation checks.

## Reproducible comparison

Use an AC-powered machine and a release binary. In a worktree build with
`make build-release-compile` (Developer Guide §7); never run `make release` in a
worktree. Archive the binary before running baseline targets, which relink a
debug executable. Do not run builds or other qualification workloads alongside
the timing matrix.

```bash
mkdir -p temp/fn_bench_before
# reconstruct the unchanged source tree, including relative imports
git archive 911a0b1ed8 test/benchmark | tar -x -C temp/fn_bench_before
python3 test/benchmark/compare_function_local.py \
  --before temp/fn_bench_before \
  --binary ./lambda.exe \
  --manifest test/benchmark/function_local_migration.json \
  --output temp/fn_bench_comparison.json --runs 5
```

Each row alternates before/after and after/before processes after warmup. JIT
uses the existing `__TIMING__` execution interval; AUTO uses process wall time
including promotion. Both variants must exit successfully and emit identical
non-timing output, including warmups. Preserve raw samples, source hashes,
both binary hashes and environment metadata. Report median paired ratios and deltas,
with bootstrap intervals. The runner defaults to the same binary on both sides; `--before-binary`
selects a separate original release so runtime fixes are included. The default regression budget is 5% plus 0.1 ms;
uncertain increases need more runs, not a passing label. Process failures are
correctness failures regardless of timing.

## Runtime findings

- Empty functional blocks allocated and finalized an empty list on every loop
  iteration. Use the established null/spreadable-null result directly, preserving
  S2.5.3v3 and S2.5.5v2. A MIR fixture forbids these allocator calls.
- AUTO planned loop continuations only for procedures. Admit eligible function
  `while` suffixes with the original callable colour. Collected values preceding
  a resumed loop require T0 unless an unconditional terminal return discards
  them; shared capture homes remain in T0. Forced-threshold fixtures cover result
  collection, explicit returns and COW container live-ins.
- Untyped scalar parameter copies exposed missing native-lane proofs: the
  return proof did not consult a live inferred parameter, and literal-count
  `shr` lacked the proof used by its native lowering. Reuse those guarded
  carriers; typed/untyped source annotations remain unchanged.
- Callable literals were pooled despite the COW proof treating literals and call
  results as fresh owners. Restrict image pooling to module literals; callable
  literals use ordinary runtime construction, matching procedures. This fixes
  rejected map stores and state shared between repeated constructor calls.

- Statement-bodied functions missed a procedure-only whole-return proof and
  were forced onto dynamic boxed return paths. Share the native and wide-free
  proofs across both colours for bodies ending in an unconditional `return`;
  retain dynamic fallthrough handling otherwise. The shared AST walk counts and
  checks every return, including handler arms, and skips nested callables
  (D2.2.2, D5.2.1v5, D8.2.6). Focused release Fibonacci pairs return to parity;
  the previous diagnostic matrix's 3–4x recursion regressions are preserved in
  `temp/fn_benchmark_migration/comparison_diagnostic1.json`.

- Private-tree producer/consumer proofs and exclusive integer dispatch were
  limited to procedures. Admit pure terminal-return tree builders and folds;
  dispatch still requires control statements so functional values are retained.
  The same proof and emitter serve both colours (S12.1.2v3, D2.2.2).
- The carrier oracle recovered native strings for procedures but only numeric
  native results for functions. Recover the actual proven carrier for either
  colour; error joins and inferred boxed fallbacks remain boxed. This restores
  string-concatenation fusion in untyped JSON generation (D2.4.1, D8.2.6).
  MIR/GC fixtures cover the optimized paths and an uncertain specialization
  caller that must still use the complete boxed fallback.

## Qualification

The qualified selection converts 419 functions in 105 files. Eight original
sources remain unchanged because their controls already fail: the two text-search
programs and `ls_micro/text_search_typed_params` crash, untyped JetStream
DeltaBlue and Richards report failed container stores, untyped JetStream N-body
fails its checksum, and untyped JetStream Base64 times out. The archived
`awfy/cd2_orig` also times out and reports failed stores; its header requires
preserving the archived source. Both original and candidate failures are retained.
The live registered variants remain in the timing matrix. The
[control failures](../../test/benchmark/function_local_baseline_failures.json)
record their observed failures without changing workloads or goldens.
The final matrix has 222 rows: 111 scripts in JIT and AUTO, including changed
standalone copies omitted by registry deduplication. Removing the five newly
failed standalone candidates preserves samples only for byte-identical retained
sources with the same archived binaries and protocol. The raw result records
the selection amendment, source hashes and original candidate-report hash;
the failed controls remain in the linked evidence.
The process helper's optional per-stream output cap bounds diagnostic floods;
uncapped callers keep their existing behavior. Output limits, timeouts and
runtime error diagnostics fail the comparison, including its warmups. The
bounded helper has success, flood and timeout tests.

### Final release comparison

All **222/222 rows pass**: 111 scripts in each tier, with identical non-timing
output, successful warmups and no runtime error diagnostics. Each row starts
with five alternating pairs; the 21 uncertain rows were extended to 21 pairs
without discarding their first samples. Sources, annotations, input sizes,
checksums and timing boundaries are preserved. Twenty converted functions copy
rebound scalar parameters to local working variables (S9.1.3v2).

| Metric across 111 scripts | Geometric mean paired ratio | Median paired ratio | Rows with 5 / 21 pairs |
|---|---:|---:|---:|
| JIT execution | 0.9886 | 1.0024 | 98 / 13 |
| AUTO process wall | 0.9913 | 1.0009 | 103 / 8 |

Ratios are migrated/original; lower means faster. These are the geometric mean
and median of the per-script median paired ratios, with each script weighted
equally. They describe the combined source migration and compiler fixes, using
separate archived release binaries. The comparison meets the predeclared budget:
a regression must exceed **both 5% and 0.1 ms** in paired medians. This permits
smaller increases. The largest increase above the time floor is 4.77% in
`ls_micro/typed_numeric_store2` (21 pairs, paired interval 1.0300–1.0642).
Two sub-millisecond probes exceed 5% with deltas of only 0.035 and 0.036 ms.

| Representative workload | JIT paired ratio | AUTO paired ratio |
|---|---:|---:|
| R7RS Fibonacci, typed | 0.9811 | 0.9914 |
| Kostya Collatz, untyped | 1.0025 | 0.9947 |
| Kostya Brainfuck, untyped | 1.0003 | 1.0126 |
| Kostya JSON generation, untyped | 1.0199 | 0.9793 |
| Larceny GC benchmark, typed | 0.9819 | 0.9919 |
| JetStream Cube3d, typed | 1.0024 | 1.0009 |
| JetStream DeltaBlue, typed | 0.9482 | 1.0177 |

[Raw results](../../test/benchmark/function_local_results.json) contain every
sample, warmup, source hash, interval and row status. Both release checks and
the AC-power check passed on macOS 26.3 arm64, 10 logical CPUs. External system
load varied; paired order and resampling limit its effect, and raw load readings
remain in the artifact. No build or qualification job from this worktree ran
during the timing matrix. The initial full selection and resampling ran on
2026-11-02; eight failing original sources remain outside the qualified
migration, with explicit controls above.

Archived release SHA-256:

- Original PN source host: `6591bc2d7781ed04d959c5c73fc3bbd620eb99b59d4d82b2ffe6c6e3f6dd87c6`.
- Migrated FN source host: `f0fedb8b7b805327fcd4634ea6d8600598d86e5be7addd5734caf4e1363615cc`.

### Emission review and runtime gates

The migrated Cube3d module shrinks from 17,056 to 16,934 MIR instructions.
Its `run_cube` body grows from 9,651 to 9,663: twelve MOVs in inlined
value/control joins, with no additional calls. Seven boxed helper wrappers
shrink by seventeen instructions each; removing the empty module collector
saves fifteen more (S2.5.3v3, S12.1.2v3, D2.2.2, D8.2.6).

Typed DeltaBlue grows from 10,882 to 11,072 module instructions, while
`constraint_choose_method` stays at 1,875. Pure callees expose borrowed aliases;
required guarded COW detaches and boundary-failure paths add 81/54/122
instructions in `constraint_execute`/`constraint_recalc`/`incremental_remove`
(S9.1.2). Three wrappers shrink by seventeen instructions, another body by one,
and the empty module collector by fifteen. The exact per-function review and
diffs are under `temp/fn_benchmark_migration/mir_review/`. Pin these reviewed
counts only in the measured default profile; Windows budgets are unchanged.

Final `make test-lambda-baseline`: **8,533/8,533 pass**, including all 20 MIR
emission-budget checks and 353 MIR GC stress tests. All 282 MIR emission
fixtures pass, including new assertions for empty loops,
native return lanes, region trees, exclusive dispatch and string fusion. Tier
fixtures pass with every-allocation GC, poisoning and forced promotion. The
bounded process helper's three contract tests pass. The precise-root hazard
audit passes for 18,561 native functions. Logs are under
`temp/fn_local_followup/`, including `baseline_qualified.log` and
`process_contracts_final.log`. The complete interpreter sweep, unchanged
preexisting mismatches and its exact source scope are recorded in the
[decomposition follow-up](<Lambda_Impl_Function_Local_Decomposition (done).md>).

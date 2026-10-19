# JS MVP Test262 stability audit

**Date:** 2026-10-19  
**Authority:** **D8.1.3v26** (MVP is the JavaScript MIR engine; dynamic source uses the retained interpreter). No semantic ruling changes.  
**Acceptance:** the complete configured suite must finish with zero failures,
timeouts, crashes, missing results or retries. A successful process exit alone
does not satisfy this gate.

**Qualified result:** **40,782/40,782 fully passed**, with **0 failures,
timeouts, crashes, partials, missing results or retries**. All **413 original
batches** completed using four workers in **1,243.1 s**. The existing **2,131
exclusions** remain unchanged. Both required baselines also pass: **7,883/7,883
Lambda/input** and **40,261/40,261 Test262**.

## Scope and reproduction

The audit uses canonical bodies and metadata from pinned Test262 commit
`673e9bacbe28590f501e2dcd817aadcc31899191`. It does not restrict selection to the
passing baseline: **40,782** tests are scheduled from **42,913** discovered
tests, including **521** beyond the usual 40,261-test baseline. Existing
unsupported-feature and explicit exclusions account for **2,131** skipped
tests. This is the repository's configured suite, not every upstream test.

```sh
LAMBDA_TEST262_EXE="$PWD/temp/js_mvp_test262_stability/final_release.exe" \
  temp/js_mvp_test262_stability/qualified_runner.exe \
  --mir-only --no-stripped --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt --jobs=4 \
  --write-failures=temp/js_mvp_test262_stability/final_full_failures.tsv \
  --feature-summary --gtest_brief=1
```

Under **D8.1.3v26**, `--mir-only` selects production MVP execution, with no
whole-program AST fallback. The run schedules **16,673 MIR-native assertion
jobs + 24,109 JavaScript-harness jobs**, with zero AST-routed jobs. Dynamic
`eval`/Function source still uses the specified retained interpreter. Original
harness includes, negative expectations, exclusions and deadlines are retained.
The audit ran with four workers and no concurrent build.

`final_qualification_provenance.json` records the executable, runner, corpus-policy
and harness hashes. The executable is a private copy of the release build so
later debug builds cannot replace the process being qualified. The exact runner
is also preserved as `qualified_runner.exe`. To create new copies before a
future audit, run `make ensure-test262-gtest`, `make build-release-compile`,
then copy `lambda.exe` and `test/test_js_test262_gtest.exe` to those two capture
paths. Do not rebuild during the timed run.

## Root cause and change

An initial standalone `--mvp-lmd` audit stopped after the 19th case in batch
267, identified from the deterministic batch inventory as the exhaustive
`General_Category=Letter` RegExp test. MVP retires a worker
after timeout/crash recovery rather than re-entering a partly unwound context;
the remaining 81 cases therefore had no results. That audit was interrupted
for diagnosis and is not counted as a completed qualification.

The shared backtracker checked a Unicode property by scanning every interval
for every input code point. Generated property tables contain hundreds of
ordered intervals. Repeated negated-property matches across the generated
all-codepoint subject dominated execution. The isolated slowdown is consistent
with deadline exhaustion under concurrent load; the interrupted audit did not
preserve that case's recovered signal/status, so its exact recovery cause is
not claimed as proven.

`RxClass.sorted_ranges` now marks standalone generated property tables, and
`bt_class_contains_raw` reuses the existing
`js_regex_sorted_range_contains` binary search. Membership changes from linear
to logarithmic interval lookup. Bracket unions retain their general path
because their interval storage can be unordered. No matcher budgets, Test262
expectations, exclusions, timeout settings or retry logic were changed.

The unchanged isolated canonical Letter test drops from **6.936 s to 2.839 s**.
Separate source instrumentation measures execution at **4.681 s before /
0.629 s after**, with the expensive negated matches dropping from roughly
600–700 ms each to 37–52 ms. These are focused measurements, not full-suite
timing claims. All ten exhaustive Unicode-property replay cases pass the original
10-second deadline; two still exceed the runner's 3-second soft threshold in
the standalone monolithic-harness profile, so this replay alone is not a
completely green aggregate gate.

## First complete audit and remaining roots

The first complete canonical MVP run took **2,353.7 s** before isolated
regression retries. It returned **40,765 fully passing / 40,782**, **7 failures**,
**10 slow partials and 10 Phase 4 retries**, with **0 timeout, crash, missing
result or Phase 2b retry**. The runner exited zero because the seven failures
were outside its saved passing baseline. This does **not** satisfy the strict
gate above. Its original logs and fresh timing/memory/partial artifacts are
preserved in `full_mir.log` and `full_mir_artifacts/`.

The seven failures identify these causes:

- `typeof import.meta` used the unresolved-identifier shortcut. Meta-property
  operands must evaluate their value just like `this` and `new.target`.
- The Script-goal early-error validator admitted `import.meta` in eval. The
  hosted evaluator then reported a capability failure instead of the required
  SyntaxError. Validation now rejects it before host admission.
- Anonymous default functions lost the parser's declaration flag. Preserving
  that flag lets the existing module instantiation path hoist default functions
  and generators before cyclic imports read their binding.
- `for…in` excluded module namespace VMAPs before their existing descriptor
  implementation could observe an uninitialized export binding.
- Module dependencies lacked realm-global Test262 helpers. The existing native
  helper installation now covers module batch entries too. Its completion
  callback exists before dependencies register Promise reactions, and helper
  closures capture the formal global after the host-global import. Test262's
  pinned `INTERPRETING.md` requires the assertion harness in the realm's global
  scope before executing a module. The runner and canonical test sources remain
  unchanged.

The seven original failures subsequently pass in the focused canonical replay
(`module7_optimized.log`). The final complete run confirms these fixes across the configured suite.

The ten slow partials in that first full audit differ from the property-escape
replay above: eight are generated Unicode identifier declaration tests and two
are RegExp match-index tests. Release CPU sampling, resolved against a matching
link map, found repeated private-name scans and single-property global shape
extension. The runtime now reuses `lib`'s typed hash map for class/name identities
and Lambda's `map_put_undefined_unique_absent_bulk_heap` for eligible global var
sets, after the original declaration checks. Function/Annex B declaration sets
retain ordered descriptor operations. MVP keeps descriptor flags in its ordinary
packed carrier rather than selecting the legacy JS DESC map kind.

Large classes containing only passive, uninitialized private instance fields
also reuse their existing constructor metadata allocation: a native loop copies
keys from registered program slots and the existing metadata initialization
supplies undefined initializers. Computed keys, methods, captures and observable
initializers retain their existing lowering. This removes redundant MIR stores
without lowering MIR optimization or changing test timing gates.

## Qualification

Evidence is under `temp/js_mvp_test262_stability/`.

| Check | Result | Evidence |
|---|---|---|
| Shared RegExp unit suite | 54/54 | `regex_tests.log` |
| Canonical Unicode replay | 10/10, no timeout/crash | `unicode10_after.log`, `unicode10_after.json` |
| First complete canonical MVP/MIR suite | strict gate failed: 7 failures, 10 retries | `full_mir.log`, `full_mir_artifacts/` |
| Original module failure replay | 7/7 | `module7_optimized.log` |
| Complete configured module subset | 651/651 | `modules_all_final.log` |
| Previously slow identifier cases | 8/8; 0.078–0.881 s wall | `native_slow8_final.log`, `native_slow8_final.json` |
| Bulk storage behavior probes | private instance and global descriptors passed, including forced GC | `bulk_probes.log`, `bulk_gc_probes.log` |
| RegExp match-index replay, four release workers | 8/8; max 1.828 s CPU | `indices_load_exact_summary.json` |
| Final complete canonical MVP/MIR suite | 40,782/40,782; 0 failures/timeouts/crashes/partials/retries | `final_full_mir.log`, `final_full_summary.json`, `final_full_artifacts/` |
| Lambda/input baseline | 7,883/7,883 | `lambda_baseline.log` |
| Standard Test262 baseline | 40,261/40,261; 0 partial/retry/failure | `test262_baseline.log`, `test262_baseline_summary.json` |

The strict summary checks the fully passing count against selection, every
original batch result count and exit status, and both retry phases. The failure
manifest has zero case rows. Final per-test timing, memory and the empty partial
list are preserved in `final_full_artifacts/`; stale batch-kill diagnostics from
the first audit are excluded by modification time. All **48 recorded runtime,
runner, corpus-policy and harness inputs** matched their hashes after execution.
The linked test corpus remains clean. No passing-baseline update was made.

Qualified runtime SHA-256:
`6af99034e5a0196a0610d2151d7e4f5b420918602e624d4c39200656a46352c5`.

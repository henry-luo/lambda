# Timeline acceptance corpus

The [proposal](../../vibe/Lambda_Pkg_Chart_Timeline.md) is the full implementation
contract. [Progress](../../vibe/impl/Lambda_Impl_Chart_Timeline.md) records work and
open gates. Runtime behavior follows S12.1.1v2/S12.1.3, S9.1.4/S9.3.1,
S7.4.1/S7.4.2, and D8.1.1v17.

`reference_inventory.json` freezes upstream commits, option-definition hashes,
declared fields, and example paths. Upstream sources are research references,
not implementation dependencies. `check_inventory.py --require-complete` is the
full-parity release check; it intentionally fails while any requirement is open.

Package contracts and their goldens live in `test/lambda/chart/timeline_*.ls`.
Run them through the real harness on each execution tier:

```sh
LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe --gtest_filter='*chart_timeline_*'
LAMBDA_EXEC_BACKEND=auto ./test/test_lambda_gtest.exe --gtest_filter='*chart_timeline_*'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='*chart_timeline_*'
```

Native fixtures live in `test/ui/timeline/` and belong to the `timeline` suite in
`test/ui/ui_test_manifest.json`. The Radiant baseline includes this suite.
The dtna migration also retains `dtna_timeline_statistic` and its independent
pixel assertions.

```sh
./test/test_ui_automation_gtest.exe --suite timeline
./test/test_ui_automation_gtest.exe --suite dtna --test timeline_statistic
python3 test/timeline/check_inventory.py
```

All temporary captures and source snapshots go under `temp/timeline-references/`.

`python3 test/timeline/run_benchmarks.py` builds the release executable in the
main checkout and records binary identity, stage timings, viewport updates,
normalization-cache reuse, peak memory, failures, and timeouts under
`temp/timeline-benchmarks/`. It covers 100, 1,000, 10,000, and 100,000 events with
uniform points, identical-date clusters, many groups, rich cards, and large years.
It refuses worktrees; debug results are not performance evidence.

Native machine results and phase traces are isolated by runner process under
`temp/ui-test-results/<pid>/`, so concurrent tier runs cannot overwrite evidence.

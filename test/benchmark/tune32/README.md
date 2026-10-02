# Tune32 evidence

Implementation and acceptance record:
[`Lambda_Tune32.md`](../../../vibe/impl/Lambda_Tune32.md).
No benchmark algorithm, iteration count, timer, or canonical source was changed
for the engine A/B measurements. Inference uses guarded entries and preserves
the complete boxed body under **D3.3.1v2, D8.3.1v2–D8.3.3**.

## Binary provenance

The implementation-start control is archived locally at
`temp/tune32/control-release`; its SHA-256 is
`dd3d0319e59cfd9b8c7515ab7ada1048028f8eea9468c98c137933c915580a1b`.
The final measured candidate is `temp/tune32/tco-release`, SHA-256
`74fec4ed49809bc27aa32a608182ffdd46cf4bd33030a13f6e10fd6ceaa51ec2`.
Both are verified ordinary release builds. `control.json` records the initial
commit and dirty-worktree boundary. Each `*-binary.json` records the exact
binary/compiler hash and cumulative compiler patch, so incremental attribution
does not depend on the mutable `lambda.exe` or a later source checkout.

## Reports

| Artifact | Purpose |
|---|---|
| `final-pilots.json` | 21 alternating pairs, four unchanged pilots and typed companions; raw samples, output hashes and paired bootstrap uncertainty |
| `final-screen.json`, `final-screen-summary.json` | Five pairs for each of 63 untyped and 63 typed rows; all rows valid, no output mismatch or slowdown above 5% |
| `bool-pilots.json`, `string-pilots.json`, `scalar-pilots.json`, `tco-pilots.json` | Nine-pair incremental attribution against the appropriate preceding archive |
| `control-diagnostics.json`, `candidate-diagnostics.json` | Balanced original / partial annotation / fully typed comparison and Levenshtein residual decomposition |
| `diagnostic_sources/`, `diagnostic_sources.json` | Annotation experiments with expected results and hashes; these do not replace canonical benchmark sources |
| `control-raytrace3d-profile.tsv`, `control-raytrace3d-cow.tsv` | Instrumented executed-work census, separate from timing; supports the conditional record-track disposition |
| `final-focused.json` | Golden-output parity in JIT, Lambda interpreter, AUTO and three existing GC stress modes |
| `final-asan.json`, `deep-tail.json` | 42 ASan executions of the final fixtures and the separate 20,000-step native-tail oracle |
| `test_mir_*-final.json`, `test_js_mir_emission_gtest-final.json` | Full baseline emission, GC and ratchet results |
| `release-ratchet.log`, `release-ratchet-diagnosis.json`, `*-release-mir.diff` | Three pre-existing release/default budget failures: control and candidate instruction counts agree; diffs contain relocated pointer constants |
| `final-lambda-baseline.log`, `final-test262-baseline.log`, `closeout.json` | Required final gates, binary identity and completed-track summary |
| `final-test262-baseline-first.log`, `test262-timing-diagnosis.json`, `unicode-full-batch.txt`, `*-unicode*.log` | First-attempt timing classification and isolated control/candidate reproduction; the unchanged final standard gate passes fully |

The remaining historical JSON files preserve the pre-implementation diagnostic
evidence. Current-control acceptance uses the explicitly named final reports.
Ratchet budgets were not changed (**D8.6.1**).

## Replay

Use the archived releases for the exact measurements:

```sh
python3 test/benchmark/run_paired_benchmarks.py \
  --control temp/tune32/control-release --candidate temp/tune32/tco-release \
  --bench primes,fast_diff,levenshtein,divrec --variants both --tier jit \
  --pairs 21 --output temp/tune32/replay-pilots.json
python3 test/benchmark/run_paired_benchmarks.py \
  --control temp/tune32/control-release --candidate temp/tune32/tco-release \
  --variants both --tier jit --pairs 5 --output temp/tune32/replay-screen.json
python3 test/benchmark/tune32/diagnose_sources.py \
  temp/tune32/tco-release temp/tune32/replay-diagnostics.json
python3 test/benchmark/tune32/validate_fixtures.py \
  temp/tune32/tco-release temp/tune32/replay-focused.json \
  test/mir/lambda/tune32_*.ls \
  test/mir/lambda/tune31_inferred_string_char_pair.ls \
  test/mir/lambda/result49_ascii_scan.ls
make test-lambda-baseline
make test262-baseline
```

`diagnose_sources.py` uses the existing benchmark runner's timed execution
parser; it adds source isolation, not a timing implementation. The fixture
replay checks committed `.txt` outputs, including the Lambda interpreter tier
that is distinct from the MIR interpreter. Final gate logs and summaries are
listed in the implementation record.

# Shell pipeline tests and benchmark

## Regression fixtures

`make test-shell-piping` runs the standalone Lambda scripts in `cases/`
through interpreter, JIT, and auto and reports elapsed CLI time for each
backend. The latest run passed 47 executions per backend, including with
forced GC. Each script has a `.txt` stdout golden. A `sha256:` golden compares
the digest of large or binary output. The Python runner prepares fixture
data in `temp/` and checks exit status, stderr, and file effects
(**S10.1.4v2**, **S14.3.4–S14.3.6v2**).

## Results from 2026-11-25

The latest timed benchmark ran the standalone workloads with the earlier
explicit-input direct command wrappers, on an Apple M4 with macOS 26.3, Bash 3.2.57,
Nushell 0.117.0, and an optimized Lambda build from `cb10b30bab` with the
shell pipeline and ordering fixes. It verified byte-identical output for each
pipeline before timing fresh processes against cached input with stdout sent
to `/dev/null`. The table reports median wall times from 31 rounds at 300 B
and seven rounds at 100 MiB.

| Pipeline | Input | Bash | Lambda auto | Nushell |
|---|---:|---:|---:|---:|
| `grep → cut → tr` | 300 B | 3.23 ms | 7.14 ms | 7.94 ms |
| `cut → sort → uniq -c` | 300 B | 3.22 ms | 7.09 ms | 7.97 ms |
| `cut → filter and uppercase → cat` | 300 B | 3.22 ms | 6.88 ms | 7.80 ms |
| `grep → cut → tr` | 100 MiB | 267 ms | 272 ms | 262 ms |
| `cut → sort → uniq -c` | 100 MiB | 523 ms | 521 ms | 526 ms |
| `cut → filter and uppercase → cat` | 100 MiB | 519 ms | 956 ms | 2.05 s |

Both all-external comparisons use the same system tools. Bash uses `awk`
for the mixed transform, Lambda uses `|:` and `|>`, and Nushell uses
`lines --strict`, `where`, `each`, and streaming `to text`. The mixed
pipeline produced 4 MiB of identical output in all three engines. Empty
process startup medians were 1.61 ms for Bash, 5.83 ms for Lambda and
6.66 ms for Nushell; these startup scripts do not have identical output.
[Latest tiny samples](timings-latest-tiny.csv) · [Latest large samples](timings-latest-large.csv)

### Retest after the ordering fix

Before the fix, the 100 MiB mixed Lambda auto pipeline took 5.94 s and hit
the five-second scheduler watchdog. After the fix, its earlier seven-trial
retest took 0.96 s; the latest run above measured 0.956 s. Auto, interpreter,
and JIT also produced the expected output before a three-second deadline.
**S14.3.3–S14.3.6v2**, **D7.5.1**, and **D8.3.1v3** cover this path.
[Historical results](results.txt) · [Historical tiny samples](timings-tiny.csv) ·
[Historical large samples](timings-large.csv) · [Diagnostics](diagnostics.txt) ·
[Watchdog trace](watchdog.txt) · [Fixed issue](<../../../vibe/Lambda_Issue_Ledger (fixed).md#interpreter-task-drain-stalls-a-returned-shell-pipeline>)

## Run the benchmark

From the repository root, build the optimized host and place the official
Nushell executable at `temp/shell-pipeline/benchmark/nu-0.117.0-aarch64-apple-darwin/nu`. [Nushell 0.117.0 release](https://github.com/nushell/nushell/releases/tag/0.117.0)

```sh
mkdir -p temp/shell-pipeline/benchmark
TMPDIR="$PWD/temp/shell-pipeline/benchmark" make build-lambda-split \
    SPLIT_PROFILE=release SPLIT_OUTPUT=temp/shell-pipeline/benchmark/lambda.exe
python3 test/shell/pipe/benchmark.py
python3 test/shell/pipe/diagnose.py
```

Use `benchmark.py --tiny-only --validate-only` for a quick check. The runners
time the standalone scripts in `workloads/` and write generated inputs and
new timing samples to `temp/shell-pipeline/benchmark/`. Set
`LAMBDA_SHELL_BENCH_EXE` to select another optimized Lambda executable.

# Text benchmark suite

These are standalone text-library benchmarks. The JavaScript files embed the
library cores because LambdaJS benchmarks are run without Node module or
file-loading APIs; `*.ls`, `*2.ls`, and `c2mir/*.c` implement the same bounded
workloads for the Lambda and native reference columns.

- fast_diff.js: multiline source-text diffs with semantic cleanup, 256 rounds
  over six old/new source pairs.
- microdiff.js: recursive nested document snapshots with arrays, dates, regular
  expressions, and changes, 512 rounds over four pairs.
- hyphen.js: Liang-pattern hyphenation over six mixed prose/HTML texts, 32
  rounds; each round creates a fresh hyphenator so word-hyphenation work is not
  hidden by the library cache.

All files emit the standard `__TIMING__:<milliseconds>` marker. The workloads
are intentionally bounded so each process stays well below the benchmark
runner's per-process limit. The ports preserve the workload shape and verify a
nonzero checksum; their text-specific checksums are recorded in the matching
`.txt` goldens and C2MIR port registry.

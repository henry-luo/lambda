# Text benchmark suite

These are standalone LambdaJS benchmarks for the text-oriented libraries already
covered by the test/js fixtures. Each file embeds the library core because
LambdaJS benchmarks are run without Node module or file-loading APIs.

- fast_diff.js: multiline source-text diffs with semantic cleanup, 256 rounds
  over six old/new source pairs.
- microdiff.js: recursive nested document snapshots with arrays, dates, regular
  expressions, and changes, 512 rounds over four pairs.
- hyphen.js: Liang-pattern hyphenation over six mixed prose/HTML texts, 32
  rounds; each round creates a fresh hyphenator so word-hyphenation work is not
  hidden by the library cache.

All files emit the standard __TIMING__:<milliseconds> marker. The workloads
are intentionally bounded so each LambdaJS process stays well below the
benchmark runner's ten-second per-process limit.

# Text benchmark suite

These are standalone text-library benchmarks. The JavaScript files embed the
library cores or load their checked-in fixture data; `*.ls`, `*2.ls`, and
`c2mir/*.c` implement the same bounded workloads for the Lambda and native
reference columns.

- fast_diff.js: multiline source-text diffs with semantic cleanup, 256 rounds
  over six old/new source pairs.
- microdiff.js: recursive nested document snapshots with arrays, dates, regular
  expressions, and changes, 512 rounds over four pairs.
- hyphen.js: Liang-pattern hyphenation over six mixed prose/HTML texts, 32
  rounds; each round creates a fresh hyphenator so word-hyphenation work is not
  hidden by the library cache.
- prettier_ast.js: a Prettier/Babel `Program` AST loaded from
  `prettier_ast.json` and printed through a small document IR, 256 rounds.
- text_search.js: repeated naive, KMP, and Boyer–Moore searches over a generated
  corpus, 1,536 rounds with cross-algorithm agreement checks.
- three_way_merge.js: line-level and word-level merges of 768 related lines,
  11,000 rounds with conflict-marker output included in the checksum.
- log_pipeline.js: mixed structured and positional log parsing, filtering,
  service grouping, and aggregation over 12,000 records for 180 rounds.

`prettier_ast_preprocess.js` parses `prettier_source.js` with the local Babel
parser used by Prettier and writes the compact AST fixture. The JavaScript and
Lambda printers consume that same JSON AST and are checked against Prettier's
formatted source during fixture development.

All files emit the standard `__TIMING__:<milliseconds>` marker. The three new
workloads are sized for approximately one second of Node.js work on the
reference machine; fixture construction is outside each timed region. The
workloads are intentionally bounded so each process stays below the benchmark
runner's per-process limit. The ports preserve the workload shape and verify a
nonzero checksum; their text-specific checksums are recorded in the matching
`.txt` goldens.

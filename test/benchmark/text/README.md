# Text benchmark suite

These are standalone text-library benchmarks. The JavaScript files embed the
library cores or load their checked-in fixture data; `*.ls`, `*2.ls`,
`c2mir/*.c`, `python/*.py`, and `../go/cmd/text/*` implement the same bounded
workloads for the Lambda and native reference columns.

- fast_diff.js: multiline source-text diffs with semantic cleanup, 256 rounds
  over six old/new source pairs. `generate_fast_diff_fixture.js` extracts the
  source pairs into `fast_diff_pairs.json` for the Go and Python ports.
- microdiff.js: recursive nested document snapshots with arrays, dates, regular
  expressions, and changes, 512 rounds over four pairs. The Lambda and C2MIR
  ports construct snapshots and diff records; they do not score precomputed
  change metadata.
- hyphen.js: Liang-pattern hyphenation over 13 mixed prose/HTML texts, 32
  rounds; each round creates a fresh hyphenator so word-hyphenation work is not
  hidden by the library cache. The cases cover ordinary patterns, dictionary
  exceptions and no-break exceptions, HTML tags/attributes, entities, explicit
  hyphens, apostrophes, repeated words, capitalization, and malformed markup.
  `hyphen_patterns.json`, `c2mir/hyphen_patterns_data.h`, `hyphen_tables.json`,
  `hyphen_cases.json`, and `test/benchmark/hyphen_tables.ls` are generated from the checked-in
  en-US library data by `generate_hyphen_fixture.js`; regenerate them whenever
  `hyphen.js` changes its embedded pattern data. The typed entry uses flat
  `int[]` trie tables, typed caches and string spans in
  `test/benchmark/hyphen_typed.ls`. Table loading/admission occurs before
  timing; scanning, cache population and output construction remain timed.
  Both Lambda cores share the exact cases and lexical predicates in
  `test/benchmark/hyphen_common.ls`. The typed representation rewrite is a
  source change; measure its gain separately from compiler/runtime tuning.
- prettier_ast.js: a Prettier/Babel `Program` AST loaded from
  `prettier_ast.json` and printed through a small document IR, 256 rounds.
- text_search.js: repeated naive, KMP, and Boyer–Moore searches over a generated
  corpus, 1,536 rounds with cross-algorithm agreement checks.
- three_way_merge.js: line-level and word-level merges of 768 related lines,
  11,000 rounds with conflict-marker output included in the checksum.
- log_pipeline.js: mixed structured and positional log parsing, filtering,
  service grouping, and aggregation over 12,000 records for 180 rounds.

- jq_mix, jq_records, jq_bf, jq_tree: jq-language workloads
  (`vibe/impl/Lambda_Impl_Jq_Tests.md`). The filters live in `jq/*.jq`, with
  their sizes inside (`def rounds`, `def depth`), and are the single source of
  truth: the Node/LambdaJS, Go and Python columns each run the *same filter
  text* on an existing jq interpreter. Those are **jqjs + patches**
  (`jq/vendor/`, see `jq/VENDOR.md`), **gojq** 0.12.19 (pinned in
  `../go/go.mod`) and **purejq** 0.3.1 (`python/vendor/`, unmodified). jq
  1.7.1 produced the goldens and gojq confirms them. Each filter reduces to
  one integer, so object key order and float formatting never reach the
  checksum.
  - jq_mix: 27 of jaq's bench filters (MIT), 7 rounds. Breadth.
  - jq_records: order processing over `jq/orders.json` (4,000 orders), 15
    rounds. Breadth on JSON data.
  - jq_bf: jaq's Brainfuck interpreter written in jq, over `jq/fib.bf`, 10
    passes. Load: generators, `until`, path updates.
  - jq_tree: `(.. | scalars) |= f`, `[paths]` and `flatten` over a 2^17-leaf
    tree, 7 rounds. Load: path machinery.

  The load rows run about 6.3 s on jq 1.7.1, and the interpreters differ a
  lot in speed (purejq needs tens of seconds), so these rows carry a 600 s
  per-row timeout floor in `run_benchmarks.py`. The Lambda MIR-U/MIR-T columns
  run each filter *translated by hand* into a Lambda query (`jq_*.ls`,
  `jq_*2.ls`). The C2MIR cell is built to hold two values: the C jq VM, and
  a typed Lambda port of the same VM (`../jq_vm.ls`, run by `jq_*_vm.ls`)
  shown as `/ λ-VM`. The `_vm` entries are held back until two runtime
  defects that exhaust memory are fixed (LR03-37, LR03-38; see the proposal
  §1.5).
  `generate_jq_fixture.js` regenerates `jq/orders.json` and
  `jq/jq_helper.js` (vendored jqjs without its ES exports, plus
  `jq/jq_driver.js`).

`prettier_ast_preprocess.js` parses `prettier_source.js` with the local Babel
parser used by Prettier and writes the compact AST fixture. The JavaScript and
Lambda printers consume that same JSON AST and are checked against Prettier's
formatted source during fixture development.

All files emit the standard `__TIMING__:<milliseconds>` marker. Fixture
construction is outside each timed region, and the workloads are intentionally
bounded so each process stays below the benchmark runner's per-process limit.
The ports preserve the workload shape and verify exact checksums; their
text-specific checksums are recorded in the matching `.txt` goldens.

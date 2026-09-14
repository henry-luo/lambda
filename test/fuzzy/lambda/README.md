# Lambda fuzz harness

Run the deterministic smoke campaign with `make fuzz-lambda`; it checks the
feature/campaign ratchets, reviewed parser gaps, parser stability, explicit
T0/JIT/AUTO parity, and forced-GC/poison/root-witness parity. The reference
grammar is `lambda/tree-sitter-lambda/grammar.js`, used only as a first cut;
the C parser remains production and neither parser wins a difference without
review (**D8.1.2v3**, **S16**).

The parser lane also mutates raw byte streams (including NUL and invalid UTF-8)
and retains only novel C-parser structural signatures under
`temp/lambda-fuzz/parser-corpus/`. This is temporary feedback input, never a
committed regression corpus. `--shard-count` and `--shard-index` partition
case ordinals deterministically for CI workers (**D1.10**).

Useful commands:

```text
make fuzz-lambda-inventory
bash test/lambda_parser_diff.sh
python3 test/fuzzy/lambda/run_fuzz.py --verify-only --gc-stress --root-witness
python3 test/fuzzy/lambda/run_fuzz.py --target parser --cases=128 --shard-count=4 --shard-index=0
python3 test/fuzzy/lambda/run_fuzz.py --cases=256 --seed=424242
make fuzz-lambda-asan duration=60
make fuzz-lambda-extended duration=3600
```

Interesting cases are content/signature-deduplicated below
`temp/lambda-fuzz/`. Re-run or reduce one without reconstructing its random
trace:

```text
python3 test/fuzzy/lambda/run_fuzz.py --replay temp/lambda-fuzz/<outcome>/<id>
python3 test/fuzzy/lambda/run_fuzz.py --minimize temp/lambda-fuzz/<outcome>/<id>
```

`corpus_manifest.tsv` is the semantic admission gate. Every row declares its
role, success/reject contract, `direct` or `run` driver, tier matrix, and
determinism policy. `parser_review.tsv` admits only content-hashed,
manually-adjudicated grammar.js/C-parser differences; a changed fixture, a
resolved difference, or a new difference fails the run (**D1.10**).

Promote a confirmed root-cause fix manually: a successful regression becomes a
normal `test/lambda/*.ls` plus its required `.txt`; a rejection regression
joins `test/lambda/negative/fuzzy_crashes/`; and a retained parser-reference
gap joins the reviewed fixture manifest. Do not commit raw `temp/` artifacts.

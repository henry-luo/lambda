# MathLive Reference Corpus

This directory retains copied upstream tests and HTML snapshots as reference
data. Lambda's legacy MathLive renderer, fixed font metrics, compatibility
probes and markup-comparison runner have been removed (Math5 Phase 12).
The upstream Jest/Playwright tests call MathLive APIs, not Lambda.

The Lambda baseline now exercises the public font-driven SVG renderer:

```bash
make test-math-corpus
node test/lambda/math/run_corpus.mjs --category FRACTIONS --jobs 2
node test/lambda/math/run_corpus.mjs --fixture-source mathlive --list
```

The runner also accepts `--fixture-source lambda-input|all`, `--limit`,
`--lambda`, `--script` and `--report`. All cases must pass. It checks successful
rendering, finite measured and emitted geometry, and self-contained SVG.
This is a geometry/serialization smoke gate, not MathLive HTML or visual
parity. Font-specific layout assertions and HTML goldens live in
`test/lambda/math/`. Historical HTML comparators in `test/latex/` remain
exploratory reference tools; their class/DOM scores do not express SVG parity.

To refresh the Lambda-derived reference formulas from `test/input/`:

```bash
node test/lambda/mathlive/extract_latex_exprs.mjs
node test/lambda/mathlive/generate_lambda_fixtures.mjs
```

Generation uses `ref/mathlive/dist/mathlive-ssr.min.mjs` to write
`__snapshots__/lambda_input_markup.snap`; running the Lambda corpus needs only
the stored snapshots and `lambda.exe`. No metrics are generated into the
production package. AsciiMath conversion tests remain reference material.

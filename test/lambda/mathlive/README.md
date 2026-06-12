# Lambda MathLive Test Adapter

This directory mirrors MathLive's upstream `test/` corpus, with class names normalized from `ML__` to Lambda's `lm_` prefix.

The upstream Jest/Playwright tests do not directly test Lambda: they call MathLive APIs. Use the adapter runner to render the MathLive markup snapshot formulas through `lambda/package/math` instead.

```bash
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs
```

Useful options:

```bash
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --list
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --category "FRACTIONS"
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --limit 10
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --strict
```

The runner:

- extracts runnable formulas and expected HTML from `__snapshots__/markup.test.ts.snap`
- writes a generated Lambda batch script to `./temp/lambda_mathlive_markup_batch.ls`
- renders formulas via `parse(..., {type: "math", flavor: "latex"})` and `math.render_inline()`
- writes comparison details to `./temp/lambda_mathlive_markup_report.json`

By default the runner reports mismatches but exits successfully. Use `--strict` when a category is ready to become a failing gate.

`math-ascii.test.ts` is a conversion suite (`convertLatexToAsciiMath()` and `convertAsciiMathToLatex()`), not a math HTML rendering suite. Lambda currently has an ASCII math parser, but no separate `format(..., "ascii")` formatter equivalent to MathLive's AsciiMath serializer, so those tests remain reference material until that formatter exists.

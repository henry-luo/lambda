# Lambda MathLive Test Adapter

This directory mirrors MathLive's upstream `test/` corpus, with class names normalized from `ML__` to Lambda's `lm_` prefix.

The upstream Jest/Playwright tests do not directly test Lambda: they call MathLive APIs. Use the adapter runner to render the MathLive markup snapshot formulas through `lambda/package/math` instead.

```bash
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs
```

Useful options:

```bash
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --list
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --category "FRACTIONS"        # upstream MathLive category
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --category "math_intensive"   # Lambda-derived (matches filename substring)
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --limit 10
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --strict
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --fixture-source mathlive
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --fixture-source all
```

The runner:

- extracts runnable formulas and expected HTML from two snap files:
  - `__snapshots__/markup.test.ts.snap` — MathLive's upstream corpus (categories: ACCENTS, FRACTIONS, etc.)
  - `__snapshots__/lambda_input_markup.snap` — Lambda-derived corpus extracted from `test/input/*.{tex,md,txt}` (categories named by source filename, e.g. `math_intensive_test.tex`)
- accepts `--fixture-source mathlive|lambda-input|all`; `make test-lambda-baseline` passes `mathlive`, while plain `make test` uses the default `all`
- writes a generated Lambda batch script to `./temp/lambda_mathlive_markup_batch.ls`
- renders formulas via `parse(..., {type: "math", flavor: "latex"})` and `math.render_inline()` / `math.render_display()`
- writes comparison details to `./temp/lambda_mathlive_markup_report.json`

By default the runner reports mismatches but exits successfully. Use `--strict` when a category is ready to become a failing gate.

## Regenerating the Lambda-derived fixture

The `lambda_input_markup.snap` file is generated from the LaTeX math expressions found in `test/input/`. To refresh it after adding new input documents:

```bash
# 1. Re-scan test/input/ for LaTeX math expressions
node test/lambda/mathlive/extract_latex_exprs.mjs
# writes temp/lambda_mathlive_extracted.json

# 2. Render each through MathLive's SSR bundle and bake fixtures
node test/lambda/mathlive/generate_lambda_fixtures.mjs
# writes test/lambda/mathlive/__snapshots__/lambda_input_markup.snap
```

The generator uses `ref/mathlive/dist/mathlive-ssr.min.mjs` (no npm install needed — the bundle and its node_modules are committed). Each formula is sent to `convertLatexToMarkup`, the `ML__` class prefix is rewritten to `lm_`, and the resulting HTML is stored alongside a `validateLatex` status code (`no-error` or the first error's code such as `unknown-command`).

Display-mode expressions (originally in `$$...$$`, `\[...\]`, or a math environment) are stored in the snap key wrapped as `\[…\]` so the runner's `normalizeFormula()` flips `display=true` after extraction.

`math-ascii.test.ts` is a conversion suite (`convertLatexToAsciiMath()` and `convertAsciiMathToLatex()`), not a math HTML rendering suite. Lambda currently has an ASCII math parser, but no separate `format(..., "ascii")` formatter equivalent to MathLive's AsciiMath serializer, so those tests remain reference material until that formatter exists.

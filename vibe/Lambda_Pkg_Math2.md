# Lambda Math Package Enhancement Proposal

## Goal

Advance `lambda/package/math/` from a working Lambda-side math HTML renderer into a MathLive snapshot-compatible renderer, while keeping Lambda's own class namespace stable as `lm_`.

This proposal builds on `vibe/Lambda_Pkg_Math.md` and the current implementation:

- `lambda/package/math/` has 2,723 lines of Lambda code across renderer, box model, metrics, spacing, symbols, and atom modules.
- Existing focused Lambda tests live in `test/lambda/math/`.
- MathLive's upstream test corpus has been mirrored from `ref/mathlive/test/` into `test/lambda/mathlive/`.
- The local class prefix has been changed from MathLive's upstream `ML__` to Lambda's `lm_`.

## Implemented Baseline Changes

### 1. Class Prefix Migration

The current renderer now emits `lm_` classes instead of `ML__` classes.

Examples:

| Old | New |
| --- | --- |
| `ML__latex` | `lm_latex` |
| `ML__mathit` | `lm_mathit` |
| `ML__cmr` | `lm_cmr` |
| `ML__mfrac` | `lm_mfrac` |
| `ML__frac-line` | `lm_frac-line` |
| `ML__strut--bottom` | `lm_strut--bottom` |

Updated areas:

- `lambda/package/math/css.ls`
- literal spacing classes in `lambda/package/math/spacing_table.ls`
- comments in `lambda/package/math/math.ls` and `lambda/package/math/atoms/array.ls`
- focused expected outputs in `test/lambda/math/*.txt`
- focused test inputs with literal class names in `test/lambda/math/*.ls`

Verification:

```bash
for f in test/lambda/math/*.ls; do
  b=$(basename "$f" .ls)
  ./lambda.exe "$f" > "temp/math-test-output/$b.txt"
  diff -u "test/lambda/math/$b.txt" "temp/math-test-output/$b.txt"
done
```

All existing Lambda math scripts match their updated expected files.

### 2. MathLive Corpus Mirror

The complete upstream `ref/mathlive/test/` tree has been copied to:

```text
test/lambda/mathlive/
```

The mirror currently contains 60 files, including:

- `markup.test.ts`
- `latex.test.ts`
- `math-ascii.test.ts`
- Jest snapshots under `__snapshots__/`
- Playwright tests
- smoke/static HTML pages
- static image fixtures

All copied MathLive expected/class references have been normalized with:

```text
ML__ -> lm_
```

The copied TypeScript tests also have their original `../src` imports retargeted to `ref/mathlive/src` from the new mirror location, so the copied suite remains usable as a reference runner.

This makes `test/lambda/mathlive/` the local Lambda comparison corpus, while `ref/mathlive/` remains the untouched upstream reference.

## Current Lambda Math Coverage

The package already has useful coverage:

| Area | Current Status |
| --- | --- |
| HTML class contract | `css.ls` constants and embedded stylesheet |
| Box model | hbox, vbox, struts, scaling, color, style |
| Metrics | simplified TeX-style sigma values and style scaling |
| Inter-atom spacing | 8x8 spacing matrix and class mapping |
| Symbols | Greek, relations, arrows, big operators, misc symbols |
| Fractions | `\frac`, `\dfrac`, `\tfrac`, binomial-style delimiters, `\genfrac` path |
| Scripts | superscript/subscript, combined scripts, big-op limit handling |
| Spacing | `\,`, `\:`, `\;`, `\!`, `\quad`, `\qquad`, `\enspace` |
| Styles/fonts | math font wrappers, text-style fallback |
| Color | basic foreground and colorbox rendering |
| Delimiters | left/right, fixed sizing commands, simple stretchy path |
| Enclosures | boxed/fbox/bbox, lap, phantom, smash, rule |
| Arrays | matrix/environment rendering, simple cases/matrix layout |
| Optimization | adjacent compatible span coalescing |

## MathLive Snapshot Categories Still Outstanding

MathLive's `markup.test.ts` covers a broader behavioral surface than Lambda currently implements. The gap is not one bug; it is a set of missing TeX semantics, parser-normalization rules, metrics, and CSS structures.

### P0: Test Harness and Comparison Contract

Before chasing individual failures, build a repeatable harness.

1. Extract formulas and expected snapshots from `test/lambda/mathlive/markup.test.ts` and `test/lambda/mathlive/__snapshots__/markup.test.ts.snap`.
2. Render each formula through the Lambda LaTeX parser and `lambda/package/math`.
3. Normalize HTML for comparison:
   - ignore insignificant whitespace
   - preserve tag order, text, class lists, styles, and data attributes
   - map only the approved prefix rule, `ML__ -> lm_`
4. Report results by MathLive category: commands, fonts, fractions, delimiters, environments, colors, etc.
5. Add a golden subset first, then expand to the full snapshot corpus.

Deliverable:

```text
test/lambda/mathlive/test_mathlive_markup.ls
test/lambda/mathlive/test_mathlive_markup.txt
```

or a small repo-local runner that generates `.ls` and `.txt` test pairs from the mirrored corpus.

Current adapter:

```bash
node test/lambda/mathlive/run_lambda_mathlive_markup.mjs
```

This extracts 206 runnable markup snapshot formulas from the mirrored MathLive snapshot file, generates `./temp/lambda_mathlive_markup_batch.ls`, renders each formula through `parse(..., {type: "math", flavor: "latex"})` and `math.render_inline()`, and writes `./temp/lambda_mathlive_markup_report.json`. The runner reports mismatches by default and only exits non-zero with `--strict`, so it can be used immediately while exact MathLive parity is still in progress.

Current phase checkpoint:

- Full MathLive markup adapter: 16/206 exact snapshot matches.
- `BINARY OPERATORS`: 10/10 exact matches.
- `FRACTIONS`: 2/9 exact matches.
- `SUPERSCRIPT/SUBSCRIPT`: 1/2 exact matches.
- `LEFT/RIGHT`: 2/55 exact matches.
- Implemented so far: direct MathLive-style root struts, inline spacer spans, TeX binary-to-ordinary operator normalization, simple superscript vlist output, transparent script sibling emission, small `\left...\right` delimiter output, TeX minus glyph normalization, and MathLive-style vlist output for simple bar fractions.
- Next blocker: port nested/scriptscript fraction metrics and richer grouped script contents; the remaining superscript case is dominated by scaled nested fraction structure and script content metrics.

### P1: Structural Rendering Fidelity

These items block many markup snapshots:

| Feature | Gap | Proposed Work |
| --- | --- | --- |
| VList layout | Current vbox is simplified | Port MathLive's vlist table/span structure more closely, using `lm_vlist-*`, `lm_pstrut`, and exact top/bottom shifts |
| Full stylesheet | Embedded CSS is a small subset | Port the static-render subset of `core.less` and font CSS under the `lm_` prefix |
| Font metrics | Width/height/depth are approximate | Port MathLive font metrics for common KaTeX families and use glyph metrics in `text_box()` |
| Class namespace | Constants are updated, CSS is still hand-maintained | Generate or centralize stylesheet selectors from class constants to avoid drift |
| Attribute output | Some MathLive snapshots rely on stable inline styles | Match MathLive style strings for fractions, scripts, radicals, rules, and vlist nodes |

### P1: TeX Core Semantics

| MathLive Category | Outstanding Work |
| --- | --- |
| Mode shift | Support `\text{...}`, `\ensuremath{...}`, and `$...$` inside text/color contexts |
| Commands | Escaped specials `\#`, `\%`, `\$`, `\_`, `\{`, `\}`, text accents inside `\text{}` |
| Binary operators | Reclassify leading `+`, operators after open/relation/punctuation, and operators after colored atoms |
| Fractions | Add `\pdiff`, robust infix `\choose`, `\over`, `\atop`, `\brace`, `\brack`, display/text fraction style switching |
| Sup/sub | Match MathLive shift formulas, cramped styles, nested fraction scripts, and left/right group scripts |
| `\not` | Render overlay negation for arbitrary following atoms, not just `\ne`/`\neq` glyph lookup |
| Spacing/kern | Implement `\hskip`, `\kern`, `\hspace{}`, full dimension parsing, `mu` units, and accent spacing preservation |

### P1: Delimiters, Surds, Accents

| Feature | Outstanding Work |
| --- | --- |
| Left/right validation | Reject invalid delimiter atoms with MathLive-compatible error codes |
| `\middle` | Size middle delimiters to the enclosing left/right group |
| Extensible delimiters | Port stacked/SVG delimiters for tall content, not only size font levels |
| Delimiter aliases | Complete `\lgroup`, `\rgroup`, moustaches, arrows, vertical variants, null delimiter |
| Surds | Match radical height, index placement, empty radical behavior, and scriptstyle radicals |
| Wide accents | Implement width-aware `\widehat`, `\widetilde`, skew, stacked accents, and over/under braces |

### P2: Environments and Arrays

MathLive covers `bmatrix`, `pmatrix`, `Bmatrix`, `cases`, `dcases`, and `array`.

Outstanding work:

- Parse and honor array column specs like `{ll}`.
- Implement per-column alignment and spacing.
- Handle ragged rows and missing cells in a MathLive-compatible way.
- Add environment validation errors for incomplete or mismatched `\begin` / `\end`.
- Add optional row/column lines for future array variants.
- Match displaystyle behavior in `dcases`.

### P2: Styling, Color, and Extensions

| Feature | Outstanding Work |
| --- | --- |
| Size commands | Implement `\tiny` through `\Huge`, including text/math interaction |
| Font variants | Complete MathLive variant repertoire for upright, bold, italic, fraktur, script, sans, monospace, blackboard |
| Xcolor names | Add the full named color table used by MathLive snapshots |
| Color formats | Parse and validate `rgb(...)`, short/long hex, and xcolor mixes like `#111!50!#fff` |
| `\bbox` options | Parse padding, border, background, and dimension options |
| HTML extensions | Implement `\htmlData`, `\class`, `\cssId`, `\htmlStyle`, and eventually `\href` with validation |

### P3: Broader MathLive Parity

These should come after the markup corpus is mostly green:

- MathLive `latex.test.ts` serialization parity.
- Static element rendering tests.
- Math ASCII tests.
- Playwright visual/smoke tests where Lambda can provide comparable static output.
- Accessibility output: MathML and speakable text hooks.
- Editing metadata: atom IDs, caret targets, and placeholder atoms.

## Proposed Implementation Order

1. **Lock the prefix contract**
   - Keep all Lambda output classes under `lm_`.
   - Keep MathLive mirror snapshots normalized to `lm_`.
   - Add a small check that fails if `ML__` reappears under `lambda/package/math` or `test/lambda/math`.

2. **Build the MathLive markup harness**
   - Start with `markup.test.ts`.
   - Generate a categorized report before attempting large feature work.
   - Land an initial golden subset: fonts, binary operators, simple fractions, scripts, radicals.

3. **Port layout primitives**
   - Make vlist, struts, rule thickness, and metrics match MathLive first.
   - This reduces churn in every downstream feature.

4. **Close P1 semantic gaps**
   - Binary operator reclassification.
   - Fractions/scripts/radicals.
   - Left/right/middle delimiters.
   - `\not`, spacing, and dimensions.

5. **Close P2 command families**
   - Arrays/environments.
   - Colors and style commands.
   - HTML extension commands.

6. **Expand the pass gate**
   - Move from golden subset to all MathLive markup snapshots.
   - Add category pass-rate targets:
     - P1 categories: 95%+ structural match
     - P2 categories: 90%+ structural match
     - Full markup corpus: 90%+ before visual work

## Definition of Done

The enhancement is complete when:

- `lambda/package/math` emits only `lm_` math classes.
- Existing `test/lambda/math/*.ls` scripts pass against expected `.txt` files.
- `test/lambda/mathlive/` contains a reproducible MathLive-derived test corpus.
- A MathLive markup runner reports pass/fail by category.
- P1 MathLive categories pass structurally against normalized snapshots.
- Remaining P2/P3 failures are tracked by exact formula and feature gap, not broad unknowns.

# Math image comparisons against pdfLaTeX

`run_texcmp.mjs` reads the upstream KaTeX `ss_data.yaml` corpus, renders each
formula with Lambda's public `lambda.doc.math.math` package (D7.2.4), and uses
Lambda's native `render` command to produce a PNG. It compiles the same formula
with `pdflatex` and rasterizes the PDF with Poppler. All generated scripts,
documents, images, logs, and reports stay under `./temp/`.

Dependencies: the built `./lambda.exe`, Node.js, `pdflatex` with `amsmath`,
`amssymb` and `xcolor`, and Poppler's `pdftoppm` and `pdfinfo`. Install the two
local Node dependencies from the project root:

```sh
npm install --prefix test/lambda/math --ignore-scripts --cache "$PWD/temp/npm-cache"
```

Run a small selection, list the corpus, or compare every case:

```sh
make test-mathcmp ARGS='--case Functions --case GreekLetters --case Exponents --case Sqrt --case BinomTest --case DisplayMode --case Colors'
make test-mathcmp ARGS='--list'
make test-mathcmp
```

The Make target builds Lambda first and forwards `ARGS` to the runner. The
runner can also be invoked directly with `node test/lambda/math/run_texcmp.mjs`.

Each invocation creates a fresh `temp/math_texcmp/run-*/` directory and prints
its HTML report path. `--out temp/custom-directory` changes the parent directory.
Each compared case retains:

- `formula.ls`, `lambda.json`, `lambda.svg`, `lambda.html`, and `lambda.raw.png`.
- `reference.tex`, `reference.pdf`, `pdflatex.raw.png`, and command/TeX logs.
- Cropped grayscale `lambda.png`, `pdflatex.png`, and a color overlay `diff.png`.

The run's `report.json` records the corpus and executable SHA-256 hashes, tool
versions, comparison settings, dimensions, offsets, errors, and source metadata.
`index.html` displays the three images at their native scale.

## What the comparison measures

Both renderers use the same em scale: by default 64 CSS pixels in Lambda and
10 TeX points at `64 * 72.27 / 10` DPI in Poppler. White margins are cropped;
an integer translation within `--align-radius` pixels per axis (default 12)
maximizes grayscale ink correlation. Images are never independently resized or
deformed. Reaching the alignment search boundary is flagged in the report.

The overlay uses upstream `texcmp`'s coloring: black is overlapping ink, red is
Lambda-only ink, and green is pdfLaTeX-only ink. `ink_error` is the sum of absolute
grayscale differences divided by union ink mass, on a scale from 0 to 1.
`mismatch_fraction` counts pixels differing by more than `--pixel-tolerance`
(default 16) over union ink pixels, excluding the white background. The ink
mask uses grayscale values below 250. Rasterization, font selection, and layout
can all contribute to differences; these metrics do not establish mathematical
equivalence or baseline alignment in surrounding prose.

By default, completed comparisons exit successfully and visual differences are
reported for review. To enforce a chosen error threshold, for example:

```sh
make test-mathcmp ARGS='--case Functions --max-ink-error 0.25'
```

Rendering errors and threshold failures exit 1. Cases marked `nolatex` upstream
are explicit skips with their original reasons. Failures do not stop later cases.
`--timeout` bounds each renderer command in milliseconds (default 60000).

## Corpus scope

The source is [KaTeX's screenshot corpus](https://github.com/KaTeX/KaTeX/blob/main/test/screenshotter/ss_data.yaml).
Strings and object entries are supported. Each case's `display` mode is honored,
and fixture macro definitions are passed to both renderers. This is a
**formula-only** comparison: browser `pre`, `post`, and `styles` fields remain
in the report but are not rendered. Upstream browser-specific behavior is not
claimed as covered.

The reference preamble loads `amsmath`, `amssymb`, and `xcolor`. Some corpus cases
require additional packages or KaTeX-specific macros; these produce visible
errors rather than substitutions. `--preamble path/to/additions.tex` appends
reference package declarations or macro definitions. Unsupported Lambda input
also remains an error. The whole corpus is not expected to pass merely because
the comparison pipeline works for the smoke selection.

The pixel helper's focused checks cover identity, translation in both directions,
extra ink, overlay colors, transparency, blank images, and tolerance handling:

```sh
npm test --prefix test/lambda/math
```

# Math image comparisons against pdfLaTeX

`run_texcmp.mjs` reads the upstream KaTeX `ss_data.yaml` corpus, renders each
formula with Lambda's public `lambda.doc.math.math` package (D7.2.4), and uses
Lambda's native `render` command to produce a PNG. It compiles a LaTeX equivalent
with `pdflatex` and rasterizes the PDF with Poppler. All generated scripts,
documents, images, logs, and reports stay under `./temp/`.

Dependencies: the built `./lambda.exe`, Node.js, `pdflatex` with the packages
listed in `texcmp_preamble.tex` and the supplementary fonts below, and Poppler's
`pdftoppm` and `pdfinfo`. Install
the two local Node dependencies from the project root:

```sh
npm install --prefix test/lambda/math --ignore-scripts --cache "$PWD/temp/npm-cache"
```

Full TeX Live includes the reference packages. For BasicTeX, install the additions
in a local user tree. This example uses the frozen TeX Live 2025 repository;
other TeX Live versions need their matching repository:

```sh
mkdir -p temp/mathcmp-texmf temp/mathcmp-texmf-tmp
tlmgr --usermode --usertree "$PWD/temp/mathcmp-texmf" init-usertree
TMPDIR="$PWD/temp/mathcmp-texmf-tmp" \
TEXMFVAR="$PWD/temp/mathcmp-texmf-var" \
TEXMFCONFIG="$PWD/temp/mathcmp-texmf-config" \
tlmgr --usermode --usertree "$PWD/temp/mathcmp-texmf" \
  --repository https://ftp.math.utah.edu/pub/tex/historic/systems/texlive/2025/tlnet-final \
  install mathtools gensymb cancel ulem etoolbox braket arydshln stmaryrd extpfeil \
  steinmetz actuarialangle pict2e unicode-math-input accents mathabx mathabx-type1 \
  stix mhchem chemgreek cjk nanumtype1 wadalab cyrillic lh lm
```

The runner discovers `temp/mathcmp-texmf` automatically and retains the existing
`TEXMFHOME` search path. TeX caches and generated fonts stay inside each run's
scratch directory. Tests never download or install dependencies automatically.

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
Each compared case under `cases/<name>/` retains:

- `formula.ls`, `lambda.json`, `lambda.svg`, `lambda.html`, and `lambda.raw.png`.
- `reference.formula.tex`, `reference.tex`, `reference.pdf`, `pdflatex.raw.png`,
  and command/TeX logs.
- Cropped grayscale `lambda.png`, `pdflatex.png`, and a color overlay `diff.png`.

The run's `report.json` records the corpus and executable SHA-256 hashes, tool
versions, comparison settings, dimensions, offsets, errors, source metadata,
and every reference syntax translation with its original source span.
`index.html` displays the three images at their native scale.

## What the comparison measures

Both renderers lay out a logical 10pt em. Lambda receives `font_size` in CSS
pixels (`10 * 96 / 72.27`), so authored `pt` lengths use the same logical scale
as TeX. The PNG wrapper then paints the SVG em at `--font-size` (default 64px),
and Poppler uses `64 * 72.27 / 10` DPI. Earlier runs left Lambda at its default
16px logical em; their point lengths and raster scores are not directly
comparable with this corrected setup. White margins are cropped;
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

## Renderer support

The public math renderer (`lambda.doc.math.math`, D7.2.4) supports inline raster
images with `\includegraphics[width=...,height=...,totalheight=...,alt=...]{...}`.
`height` measures above the baseline; `totalheight - height` is the depth.
Without an explicit width, the image keeps its intrinsic aspect ratio. The
default height is `0.9em`; unitless image dimensions use big points (`bp`).
Images are embedded in the emitted SVG. Pass `base_uri` as the directory for
relative image paths; the comparison runner uses each case's artifact directory.
Missing images, invalid dimensions, and unsupported options return errors.

Math input expands scoped TeX definitions such as `\def` and `\newcommand`
through the existing TeX engine. `\xrightleftarrows`, `\xrightequilibrium`, and
`\xleftequilibrium` produce paired arrows with optional lower and required upper
labels. Ordinary-font arrow stretching extends the shaft while preserving the
head shape. These behaviors have focused `.ls` regressions alongside this runner.

The renderer selects `\mathchoice` branches using the current math style, honors
scoped size declarations (`\tiny` through `\Huge`, including script sizes), and
supports explicit atom classes such as `\mathbin` and `\mathrel`. AMS `gathered`
and `alignedat` retain their row structure and display style; starred matrices
accept `[l]`, `[c]`, or `[r]` column alignment. Matrix rows use minimum strut
dimensions and AMS line spacing. Unbraced kern dimensions stop at their unit,
preserving adjacent variables and scripts.

Text boxes retain nested text commands and `$...$` math; `\verb` preserves its
delimited source, including ampersands inside arrays. Raised, reflected, and
vertically centered boxes, cancellation/strikeout, phase and actuarial angles,
extensible arrows, upper/lower brackets and accents, primes, modular operators,
and equation tags have dedicated rendering. Arrays retain solid/dashed rules,
subarrays and substacks, and scope infix fractions to individual cells. `CD`
diagrams retain horizontal/vertical arrows, equalities, and labels. These command
forms follow [KaTeX's supported functions](https://katex.org/docs/supported).
Tag placement, line wrapping, and font/spacing parity remain visual review items;
nonprinting controls such as `\nonumber` and `\allowbreak` do not paint their names.

Layout authority is TeX and the supported LaTeX package definition, as required
by [Math Design Rule 1](../../../vibe/Lambda_Pkg_Math.md). KaTeX/MathLive outputs
and screenshot differences are comparison evidence. Recognizable commands do
not establish conforming layout; remaining array, arrow, accent and decoration
gaps are recorded in the design. Rule 2 requires the existing bundled fonts.

## Corpus scope

The source is [KaTeX's screenshot corpus](https://github.com/KaTeX/KaTeX/blob/main/test/screenshotter/ss_data.yaml).

Strings and object entries are supported. Each case's `display` mode is honored,
and fixture macro definitions are passed to both renderers. This is a
**formula-only** comparison: browser `pre`, `post`, and `styles` fields remain
in the report but are not rendered. Upstream browser-specific behavior is not
claimed as covered.

The reference setup follows [KaTeX's pdfLaTeX template](https://github.com/KaTeX/KaTeX/blob/main/test/screenshotter/test.tex),
including its logo and math sizing definitions. Additional standard packages
provide dashed arrays, commutative diagrams, brackets, cancellations, actuarial
angles, and Steinmetz phase notation. `unicode-math-input` accepts Unicode math
symbols without changing the reference math font encoding. The reference box
preserves verbatim input and uses its own register so formula macros can use TeX's
scratch boxes. Missing glyphs fail compilation instead of disappearing silently.

The upstream YAML and Lambda input are unchanged. `texcmp_dialect.mjs` translates
KaTeX syntax into pdfLaTeX equivalents and records each change in JSON and HTML:

- CSS hex colors become explicit xcolor HTML colors.
- Verbatim input becomes literal monospaced glyphs, preserving visible spaces,
  special characters, and script sizing even inside arrays.
- Combining text accents become standard TeX accents. Unicode math alphabets
  keep their weight and shape, including in text and nested alphabet selectors.
- Cyrillic text selects T2A/LH fonts; Korean and Japanese text select real CJK
  fonts (Nanum Myeongjo and Wadalab Mincho).
- Rules accept `mu` dimensions using the current math style's symbol-font quad;
  ordinary dimensions such as `em` keep their standard TeX meaning.

Supplementary reference macros preserve symbols outside script/fraktur font
repertoires. Only the missing MathABX accents and STIX closed-integral glyphs are
selected from those fonts, retaining Computer Modern for other symbols. AMS
glyph fills provide extra arrow/segment accents; mhchem supplies the chemistry
arrows. Feature packages are selected from actual commands, never fixture names.
These font and macro choices provide a reviewable reference rather than a claim
that pdfLaTeX and KaTeX use identical glyph designs. Each case's `reference.tex`
contains its complete reference setup. The upstream image used by `Includegraphics`
is bundled with its license and mirrored into each run for both renderers.

Unknown commands and missing glyphs remain fatal errors.
`--preamble path/to/additions.tex` appends reference package declarations or macro
definitions. Unsupported Lambda input also remains an error; a completed
comparison does not establish visual equality.

The focused checks cover pixel alignment and overlays, plus actual pdfLaTeX
compilation and Poppler rasterization of previously failing corpus formulas.
They also check syntax audit spans, verbatim literals, trailing comments, custom
definitions, rendered alphabet fallbacks, math-unit widths against native TeX
kerns, and fatal errors for undefined commands and missing glyphs. Reference checks are
explicitly skipped if their executables are unavailable; missing packages fail:

```sh
npm test --prefix test/lambda/math
```

This command also runs `tex_conformance.test.mjs`: 260 independently executed
TeX boxes and 130 relations for AMS modulo glue/scope, parenthesis phantoms, sized
delimiter classes, middle-boundary binary normalization, and continued-fraction
alignment/text struts. It checks all
four styles and both AMS display-flag values. TeX's shipped numerator positions
are compared with the measured SVG glyph positions; glyph-font differences
are excluded through width deltas. Named-size probes explicitly select the
bundled companion's scaled CM10 profile. This is a bounded conformance check,
not a certificate for all math constructions. Its scripts, logs, generated PDF,
package version records, and relation data stay under `temp/math-conformance-*`.
Run it alone with `node --test test/lambda/math/tex_conformance.test.mjs`.

`tex_delimiter_conformance.test.mjs` independently executes 820 cases using
TeX's actual delimiter construction. It compares box dimensions and shipped
DVI glyph positions/sizes against Lambda SVG geometry for 26 bundled shapes
across four styles, four explicit sizes, three automatic demands, and cramped,
nested, empty and middle-boundary cases. The reference selects scaled CM10
roman characters and fixed-size CMEX10 to match the declared companion profile.
Original CMEX baseline positions are recovered from the existing KaTeX fonts'
authored encoding translations, not outline bounds or image fitting. Scripts,
DVI, font hashes, source/binary hashes and detailed relations remain under
`temp/math-delimiter-oracle-*`. Run it alone with:

```sh
node --test test/lambda/math/tex_delimiter_conformance.test.mjs
```

The checks also run the comparison pipeline for both image and reaction-arrow
fixtures. They require a built `lambda.exe`, verify all eight embedded logos in
native PNG output, and check the rendered arrow glyphs instead of command text.
Five additional native cases in `delimiter_cases.yaml` check tall parentheses,
brackets, braces, finite angle variants and the logical point scale. Assembled
tips and brace middles must survive native painting without interior gaps;
these checks supplement the independent geometry oracle.

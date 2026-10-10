# Computer Modern math companion parameters

These five original TFM files supply TeX family 0 (roman), family 2 (math
symbols) and family 3 (math extension) metrics for Lambda's default bundled
CMU profile. They are copied unchanged from TeX Live 2025 Basic, package
`cm`, directory `texmf-dist/fonts/tfm/public/cm/`, on 2026-10-09 (`cmr10.tfm`
added 2026-10-10).

Upstream: [Computer Modern](https://ctan.org/pkg/cm), by Donald E. Knuth;
[original font sources](https://ctan.org/tex-archive/fonts/cm/mf).
License: [Knuth License](https://ctan.org/license/knuth). Redistribution of
unchanged files is permitted; modified files must be renamed. The original
filenames and bytes are preserved here. No upstream source is modified.

| File | SHA-256 |
|---|---|
| `cmr10.tfm` | `87f2d8981927644cbecaf3d639e96e348ea4e7be49d8804468bd8ba9ff3f5244` |
| `cmsy10.tfm` | `0ca13d421ac7133271aed7c935099ecf3d1d08ac9e15f81acb34a16564ab8a46` |
| `cmsy7.tfm` | `4f59bbf683e947c807158641621982dc83f55fec042fea7963d8ce9f9da98256` |
| `cmsy5.tfm` | `6b7dc18cba9ca8996d7b146f1ed78d9227577b26248083310fec227692198ca5` |
| `cmex10.tfm` | `0890bccea1dd4d27f001ac30e86c63af35bc803e0557c35aafb1903c8d208e92` |

The companion is selected only for the bundled CMU profile in `bundled.ls`.
The paired Serif faces under `package/latex/fonts/Serif/` at this revision are:

| Face | SHA-256 |
|---|---|
| `cmunrm.woff2` | `6bff6a85282405b43106ade6b090fe6c3d04fdfe650cd60a1c21817293500568` |
| `cmunti.woff2` | `15d169907068aed1bae9b51fd461a62705987800b9b604dcaa86fc8fc31c4f` |
| `cmunbx.woff2` | `4c5c7e0fecd2cae39a24e41ffb9ef0c5be2ed770bf6513a6974093680035502c` |
| `cmunbi.woff2` | `73878502fa9bc4a76667586c05e96621c89127463e5b365d761a360488d2a3db` |

`tex_metrics.ls` reads the TFM font-parameter block and the logical height of
CMEX slot `0x7A` (`\braceld`), decoding signed 12.20 fixed-point values. The
height supplies mathtools' bracket rule thickness; it is not an outline-bound
estimate or a fitted constant. The symbol fonts use plain TeX's 10/7/5 point
selection, hence 100/70/50 percent sizes. Plain TeX selects `cmex10` at 10pt
in all styles, so its extension parameters remain constant across styles.
Parameter identities and algorithms come from
[TeX82 `tex.web`](https://tug.ctan.org/systems/knuth/dist/tex/tex.web), the TFM font-parameter format and math-list conversion algorithms.

Vertical arrow delimiters use the original CMSY small-character metrics and
CMEX extension recipes, including their top, bottom and repeat character
slots. `KaTeX_Main` paints the small arrows; `KaTeX_Size1` paints the unchanged
CMEX pieces at its Unicode encoding. Size1 places these pieces' baselines at
the bottom, while CMEX places them at the top; the original TFM depth restores
that baseline before assembly. TeX82 `var_delimiter` supplies the integer
repeat count, butt joins, width and axis centering. No arrowhead is stretched.
For AMS explicit big delimiters, `amsmath`'s `bBigg@` uses a text-style box
and `1.2 * (height + depth)` of the roman parenthesis. `cmr10.tfm` slot `0x28`
supplies this math-strut extent. The four size multipliers are 1, 1.5, 2 and 2.5.
`test/lambda/math/tex_delimiter_reference.tex` independently checks all six
vertical arrows at those sizes, plus script-style and automatic delimiters.

This is an explicitly selected Computer Modern math companion for CMU's
Computer Modern text outlines. It does not claim that CMU contains a MATH
table or that CMU glyph bounds equal the original TeX fonts. Advances,
outline bounds, italic faces and painted text continue to come from the
actual CMU/KaTeX resources. Explicit supplied/installed fonts retain their
own MATH parameters or MathML fallback and never inherit this companion.
Resource acquisition remains in Lambda IO (**D7.1.2v2**).

The independent reference in `test/lambda/math/tex_reference.tex` can be run
with an installed TeX engine. `test_math_tex_metrics.ls` checks frozen TeX
font parameters against painted script baselines, sizes and rules. TeX is
not a production dependency.

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

The 26 supported bundled delimiter shapes use original family-0/CMSY small
metrics, CMEX next-larger chains and extension recipes, including top, middle,
bottom and repeat character slots. Family 0 uses scaled `cmr10` at all three
sizes because the approved companion does not contain smaller roman optical
fonts; CMSY uses its actual 10/7/5 selections. CMEX remains text-size in every
style. The independent reference explicitly selects this same profile.

Existing `KaTeX_Main` paints the small characters and Size1–Size4 paint the
designed variants and pieces at their Unicode encodings. Their character
mappings and baseline translations come from the font project's
[`src/fonts/makeFF`](https://github.com/KaTeX/katex-fonts/blob/master/src/fonts/makeFF)
(reviewed 2026-10-10). These are authored font-generation translations, not TFM
depth estimates or inferred outline bounds. Reversing them restores the CMEX
baseline before composition. The font generator supplies encoding facts only:
TeX82 `var_delimiter` supplies selection, integer repeat counts, balanced brace
middles, butt joins, width and axis centering. Finite chains stop at their
largest design. No complete glyph is stretched.
For AMS explicit big delimiters, `amsmath`'s `bBigg@` uses a text-style box
and `1.2 * (height + depth)` of the roman parenthesis. `cmr10.tfm` slot `0x28`
supplies this math-strut extent. The four size multipliers are 1, 1.5, 2 and 2.5.
`test/lambda/math/tex_delimiter_reference.tex` independently checks all six
vertical arrows at those sizes, plus script-style and automatic delimiters.
`test/lambda/math/tex_delimiter_conformance.test.mjs` adds 820 independently
executed TeX cases over all 26 shapes, comparing box dimensions and shipped
DVI component positions/sizes with Lambda's measured SVG geometry. Its
artifacts retain the installed reference TFM paths and hashes. e-TeX's
[`middle` definition](https://github.com/TeX-Live/texlive-source/blob/trunk/texk/web2c/etexdir/etex.ch)
supplies the shared demand, close/open boundary roles and context restoration.

The same original TFM files now supply the bundled radical and finite wide
accent constructions. `fontmath.ltx` declares CMSY/CMEX slot `0x70` for
`\sqrtsign`. The existing small surd in `KaTeX_Main` reverses makeFF's
760-unit translation. CMEX slots `0x70`–`0x73` use Size1–Size4 translations
810/1110/1410/1710; its extension recipe uses top `0x76` (U+E001, 565), repeat
`0x75` (U+E000, 605) and bottom `0x74` (U+23B7, 915). These values describe
font encoding only. TeX82 `make_radical` supplies demand, clearance, the
selected sign-height rule thickness, baseline and extra top kern. LaTeX/
amsmath's default `r@@t` supplies scriptscript degrees, `5mu`/`-10mu` kerns
and `.6*(height-depth)` raise; no MATH degree percentage is substituted.

TeX82 `make_math_accent` selects the last fitting finite CMEX variant,
retaining its first variant for a narrower nucleus. Hats use slots
`0x62`–`0x64` and tildes `0x65`–`0x67`, painted as spacing U+02C6/U+02DC in
existing Size1–Size3. These spacing encodings have no baseline/horizontal
translation; the combining alternatives do. The larger Size4 accent designs
are AMS additions and are outside the original CMEX chains. Accent placement
uses the extension font's x-height at text size in every style. Character
skew/italic attachment remains unverified; no CMMI companion was added.

`tex_radical_conformance.test.mjs` independently ships 472 root/accent boxes
with TeX, comparing dimensions, component identities, baselines, sizes and
rules. It includes lowered nuclei, empty/lowered degrees and all four styles;
native PNG checks cover tall radical joins and finite accent painting.
Artifacts record both reference and production resources and reference macro
hashes. No font or metric files were added for these constructions.

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

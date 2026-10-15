# KaTeX resources

`KaTeX_Main-Regular.woff2` supplies matching harpoon glyphs for reaction arrows
in the bundled math profile (D7.2.4). It is copied without modification from
[`ref/mathlive/css/fonts/KaTeX_Main-Regular.woff2`](https://github.com/arnog/mathlive/blob/8342e1cb75e7828ef7cb092ef42887858215e669/css/fonts/KaTeX_Main-Regular.woff2).

- MathLive revision: `8342e1cb75e7828ef7cb092ef42887858215e669`.
- SHA-256: `c2342cd8b869e01752a9321dc17213fc40d4d04c79688c1d43f2cf316abd7866`.
- Upstream font project: [KaTeX fonts](https://github.com/KaTeX/katex-fonts).
- License: [MIT](LICENSE.KaTeX).

The existing `KaTeX_Size1`–`KaTeX_Size4` resources supply designed CMEX delimiter
variants and extension pieces, in addition to larger operators. All four files
were already bundled before the delimiter continuation; registering Size3/Size4
for these constructions adds no font files or families. The files are unchanged
copies from the same MathLive revision, under `css/fonts/`:

| File | SHA-256 |
|---|---|
| `KaTeX_Size1-Regular.woff2` | `6b47c40166b6dbe21a5dfca7718413f2147fd2399be1ba605d8ad39cedf25dfe` |
| `KaTeX_Size2-Regular.woff2` | `d04c54219f9eaec6d4d4fd42dfb28785975a4794d6b2fc71e566b9cd6db842dd` |
| `KaTeX_Size3-Regular.woff2` | `73d591271b1604960cb10bb90fee021670af7297017e0e98480b332d11f51995` |
| `KaTeX_Size4-Regular.woff2` | `a4af7d414440a1c1790825cfb700cf9cf43b0f2c4b04f0ebc523011ad9853ec0` |

Small delimiters use `KaTeX_Main`; variant chains, extension recipes and
original logical metrics come from the matched Computer Modern TFM resources
below. Encodings and authored baseline translations are documented there too.

The existing Computer Modern TFM resources are documented in
[`tex/PROVENANCE.md`](tex/PROVENANCE.md).

Closed multiple integrals reuse the existing `KaTeX_Size1` / `KaTeX_Size2`
double/triple integral glyphs and `KaTeX_Main`'s big circle. The bundled TeX
overlay definition is `test/lambda/math/tex_bundled_integrals.tex`; it does not
require another font. Math package Design Rule 2 prohibits adding fonts for
math support (**D7.2.4 / D7.1.2v2**).

# KaTeX resources

`KaTeX_Main-Regular.woff2` supplies matching harpoon glyphs for reaction arrows
in the bundled math profile (D7.2.4). It is copied without modification from
[`ref/mathlive/css/fonts/KaTeX_Main-Regular.woff2`](https://github.com/arnog/mathlive/blob/8342e1cb75e7828ef7cb092ef42887858215e669/css/fonts/KaTeX_Main-Regular.woff2).

- MathLive revision: `8342e1cb75e7828ef7cb092ef42887858215e669`.
- SHA-256: `c2342cd8b869e01752a9321dc17213fc40d4d04c79688c1d43f2cf316abd7866`.
- Upstream font project: [KaTeX fonts](https://github.com/KaTeX/katex-fonts).
- License: [MIT](LICENSE.KaTeX).

`KaTeX_Size1-Regular.woff2` supplies the CMEX arrowhead and shaft pieces for
vertical delimiters. It is copied unchanged from the same MathLive revision,
path `css/fonts/KaTeX_Size1-Regular.woff2`, with SHA-256
`6b47c40166b6dbe21a5dfca7718413f2147fd2399be1ba605d8ad39cedf25dfe`.
The small vertical arrows use `KaTeX_Main`; extension recipes and original
logical metrics come from the matched Computer Modern TFM resources below.

The existing Computer Modern TFM resources are documented in
[`tex/PROVENANCE.md`](tex/PROVENANCE.md).

Closed multiple integrals reuse the existing `KaTeX_Size1` / `KaTeX_Size2`
double/triple integral glyphs and `KaTeX_Main`'s big circle. The bundled TeX
overlay definition is `test/lambda/math/tex_bundled_integrals.tex`; it does not
require another font. Math package Design Rule 2 prohibits adding fonts for
math support (**D7.2.4 / D7.1.2v2**).

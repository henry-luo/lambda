# Radiant Issue Ledger

Working issue record for Radiant. Area numbers follow the
[`RAD_00`–`RAD_22` design documents](../../doc/dev/radiant/RAD_00_Overview.md).
Entries originating here use an `L` suffix, such as `RAD07-L1`, to avoid
collisions with numbered issues in those documents. Resolved entries retain
stable anchors and their verification evidence.

## Resolved issues

<a id="rad07-l2"></a>

### RAD07-L2 — LaTeX document transform omits bundled font declarations — RESOLVED

**Found, fixed and verified:** 2026-10-08, on macOS in the working tree.

**Symptom before the fix.** Rendering `test/input/enhanced_test.tex` selected Times Italic for
math variables, STIX Two Math for symbols/digits, Georgia for prose, and Times
Roman for the `KaTeX_Size1` fallback. The bundled fonts exist and load correctly
when explicitly declared; this is separate from the fixed raster coverage
defect [RAD07-L1](#rad07-l1).

**Root cause and regression origin.** Commit `7fe950bf3` (2026-09-21,
“made LaTeX and Graphs loading config driven”) replaced the dedicated LaTeX
loader with the shared document-transform loader and removed its explicit CMU
and KaTeX font-stylesheet loading. The replacement
[`wrap_standalone`](../../lmd/package/latex/latex.ls) embedded layout/math CSS but
no font declarations or stylesheet links. The shared loader in
[`load_lambda_document_doc`](../../radiant/cmd_layout.cpp) collects the
generated HTML's CSS without adding those package assets.

[`local_font_families`](../../lmd/package/math/css.ls) named Computer
Modern and KaTeX families, but naming a family does not register the bundled
font files. macOS's default discovery in
[`font_platform.c`](../../lib/font/font_platform.c) searches system/user font
directories rather than the package directories. The missing declarations
therefore cause system fallback. The omission is in shared code; the fallback
faces depend on the platform and installed fonts.

**Verification.** A 64px HTML probe with identical content and font-family
rules was laid out twice. Adding only links to
[`cmu-combined.css`](../../lmd/package/latex/fonts/cmu-combined.css) and
[`katex.css`](../../lmd/package/math/katex.css) changed the actual loaded faces:

| Probe | Without bundled CSS | With bundled CSS |
|---|---|---|
| Italic math variables | `Times-Italic` | `CMUSerif-Italic` from `Serif/cmunti.woff2` |
| Large integral | `Times-Roman` | `KaTeX_Size1-Regular` from `fonts/KaTeX_Size1-Regular.woff2` |

Both layouts succeeded. Before the fix, the native `.tex` layout reproduced
the system fallback and HTML conversion emitted zero font declarations or
stylesheet links. Local reproduction files and font-loading logs are in
[`temp/math-font-selection/`](../../temp/math-font-selection/).

**Fix.** The shared
[`font_stylesheets`](../../lmd/package/math/css.ls) helper emits links to both
bundled stylesheets, resolving the installation through `sys.lambda.home`
and the existing path helper, per **D7.2.4**. LaTeX, standalone math, TikZ,
the document viewer and the edit shell use it. The stylesheets retain their
own source URLs, so their relative WOFF2 URLs resolve beside the CSS assets.
Fragment renderers continue to leave stylesheet ownership to their host.

**Fix verification.** Native `.tex` layout and layout of `convert
--full-document` HTML now load `CMUSerif-Italic` and `KaTeX_Size1-Regular`
from the bundled files. Chrome 154's platform-font inspection of the exported
HTML confirms both as custom fonts and CMU Serif Roman for prose. The new
[`test_latex_bundled_fonts.ls`](../../test/lambda/latex/test_latex_bundled_fonts.ls)
regression checks native document output, serialized/reparsed standalone HTML,
standalone math, absolute/readable font paths, and fragment behavior; it fails
before the fix. The initial focused LaTeX/math/edit run passed 86/86; the final
isolated script run passed 1,242/1,244. All 141 standard-library tests, 921
MathLive comparisons, three native LaTeX view/iframe checks, and the Test262
runner-contract check pass. The font regression also passes from a different
working directory with spaces in `LAMBDA_HOME`. A rendered sample is available
locally as
[`fixed-sample.png`](../../temp/math-font-selection/fixed-sample.png).

**Baseline caveat.** Concurrent builds removed the shared executable during
the full baseline runs, so failed suites were rerun against an isolated copy.
The remaining `edit_markdown_adapter` and `edit_view_only` failures concern
Markdown block locations and footnote projection. Both reproduce against the
same executable with this font change removed from a temporary package copy;
no expectations or tolerances were changed to accommodate them.

<a id="rad07-l1"></a>

### RAD07-L1 — Faint math glyphs in the macOS grayscale rasterizer — RESOLVED

**Fixed and verified:** 2026-10-08, in the working tree.

**Symptom and font identification.** Mathematical expressions in
[`enhanced_test.tex`](../../test/input/enhanced_test.tex) had visibly faint
strokes, especially small italic variables. The sample actually resolved math
variables to **Times Italic** (`Times-Italic`), symbols/digits to **STIX Two
Math**, and surrounding prose to **Georgia**. The generated document did not
load the bundled Computer Modern fonts despite listing math families in CSS.
Chrome's platform-font inspection confirmed the same Times Italic face for
the isolated comparison.

**Root cause.** In
[`font_rasterize_ct_render`](../../lib/font/font_rasterize_ct.c), italic faces
explicitly disabled CoreGraphics font smoothing. The rasterizer also drew
white glyphs on black and unconditionally squared the grayscale values using
`(v * v + 128) / 255`. This applied an LCD-style gamma conversion to native
DeviceGray output, reducing edge coverage and erasing thin strokes.

**Fix.** Enable native smoothing for upright and italic faces, draw grayscale
glyphs as black on white, and obtain mask coverage with `255 - v` without an
additional gamma transform. Color-glyph handling and layout metrics are
unchanged. The font-pipeline record marks the earlier gamma approach as
[superseded](Radiant_Design_Font_Text.md).

**Chrome pixel comparison.** Captured the same Times Italic glyphs
(`E m c x d π e 2`) at fixed positions in Radiant and Chrome headless shell
154.0.8037.57, with black text on white. Tested 8/16/24/48/96 CSS px at 1× and
2× device scale. Representative results below require no positional alignment
or resampling. “Ink difference” is the difference in summed darkness relative
to Chrome; negative means fainter. “Pixel error” is the sum of absolute pixel
differences divided by Chrome's total ink, not by the whole image area.

| Size / scale | Ink difference before → fixed | Pixel error before → fixed |
|---|---:|---:|
| 96px / 1× | −17.55% → +0.60% | 17.55% → 0.83% |
| 16px / 1× | −44.01% → +3.30% | 44.01% → 3.99% |
| 96px / 2× | −9.36% → +0.32% | 9.36% → 0.44% |
| 16px / 2× | −34.56% → +1.63% | 34.56% → 2.20% |

**Regression coverage.**
[`FontMetricTest.RasterPreservesNativeGrayscaleCoverage`](../../lib/test_font_path_gtest.cpp)
compares production masks against independently drawn native RGB CoreText
text. All 96 comparisons pass: Times Italic and bundled CMU regular, italic,
and bold italic; four characters; 8/16/96px; 1×/2× scale. The test fails with
the original rasterizer. `make build` and all 265 view-reuse tests also pass.

**Validation limits.** `make test-radiant-baseline` reports 4,094 passed,
350 partially passing, and one failure. The remaining `pp_btn_shapes_01`
visual comparison measures 1.85% mismatch against a 1.84% limit; changing the
rasterizer increases mismatching pixels from 256 to 267 in the isolated
before/after check. References and tolerances were not changed. Small
baseline-position offsets at some sizes and color-dependent tonal differences
on dark backgrounds remain; this fix does not claim pixel-identical Chrome
rendering in every case.

**Local diagnostic artifacts** (temporary, not tracked):
[interactive pixel comparison](../../temp/math-render-quality/comparison.html),
[comparison image](../../temp/math-render-quality/comparison.png),
[corrected sample](../../temp/math-render-quality/after.png), and
[full baseline log](../../temp/math-render-quality/final-baseline.log).

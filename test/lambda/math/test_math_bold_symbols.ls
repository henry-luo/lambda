// test_math_bold_symbols.ls — glyph sources for shared code points, and bold symbols
// (Lambda_Pkg_Math §5.1, §10.1).
// - Plain TeX symbols take their cmsy shapes from KaTeX_Main before KaTeX_AMS: \cdot is
//   the axis-centred dot, not AMS's \centerdot on the baseline. \varnothing keeps the
//   AMS shape by name, since it shares U+2205 with \emptyset.
// - \boldsymbol draws a symbol from the bold upright face when it has the glyph; \mathbf
//   reaches only letters and digits; \bm falls back to poor man's bold; a \boldsymbol
//   symbol no bundled face has in bold keeps its regular glyph and is listed.
import math: lambda.doc.math.math

fn ast(src) => parse(src, {type: "math"}) ^ { null }
fn svg(src) => (format(math.render_inline(ast(src)) ^ { "ERR" }, 'html') ^ { null }) or "ERR"

// each painted glyph as "family weight style char"
fn glyphs(src) {
    let body = svg(src)
    let start = index_of(body, "</style>")
    let painted = if (start == null) body else slice(body, start, len(body));
    [for (part in split(painted, "<text ") where index_of(part, "font-family=\"") != null) (
        let attr = (key) => (let at = index_of(part, key ++ "=\"") + len(key) + 2,
            slice(part, at, at + index_of(slice(part, at, len(part)), "\""))),
        let close = index_of(part, "</text>"),
        attr("font-family") ++ " " ++ attr("font-weight") ++ " " ++ attr("font-style") ++ " " ++
            slice(part, index_of(part, ">") + 1, close))]
}

"cdot:"; glyphs("a \\cdot b")
"plain symbols:"; glyphs("\\setminus \\models \\propto \\triangle")
"empty sets:"; glyphs("\\emptyset \\varnothing")
"boldsymbol:"; glyphs("\\boldsymbol{x+1=\\times\\pm}")
"mathbf:"; glyphs("\\mathbf{x+1}")
"boldsymbol without a bold glyph:"; glyphs("\\boldsymbol{\\nabla\\cdot}")
"bm poor bold:"; glyphs("\\bm{\\nabla}")
"bm with a bold glyph:"; glyphs("\\bm{+}")
"regular bold symbols:"; [math.regular_bold_symbols(ast("\\boldsymbol{\\nabla}\\cdot\\boldsymbol{x+\\infty}"))]
"none without boldsymbol:"; [math.regular_bold_symbols(ast("\\nabla \\bm{\\nabla}"))]

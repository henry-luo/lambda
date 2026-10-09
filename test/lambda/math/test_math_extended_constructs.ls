// exercise parsed commands whose fallback previously painted their names literally.
import math: lambda.doc.math.math
import latex: lambda.latex.latex

fn box(source, options = null) => math.render_box(parse(source, 'math')^, options)^
fn paints(result) {
    let html = format(result.element, 'html');
    contains(html, "<path") or contains(html, "<text")
}

let x = box("x")
let g = box("g")
let triple_dot = box("\\dddot{x}")
let quad_dot = box("\\ddddot{x}")
let below = [for (command in ["underleftarrow", "underrightarrow", "underleftrightarrow"])
    box("\\" ++ command ++ "{g}")]
let phantom = box("\\phantom{g}")
let horizontal = box("\\hphantom{g}")
let vertical = box("\\vphantom{g}")
let smashed = box("\\smash{g}")
// compare semantic spellings in one face that supplies the Unicode math alphabet.
let math_font = {font_family: "ExtendedConstructs", fonts: [{font_family: "ExtendedConstructs",
    data: input("test/lambda/math/fonts/NotoSansMath-Regular.ttf", 'binary')^}]}
let bold = box("\\boldsymbol{A}", math_font)
let unicode_bold = box("𝑨", math_font)
let references = latex.render_string_to_html("\\documentclass{article}\\usepackage{amsmath}\\begin{document}\\begin{align}x&=1\\tag{Value}\\label{eq:value}\\end{align}\\eqref{eq:value}\\eqref{missing}\\end{document}")

let checks = [
    {name: "triple-dot accent preserves advance and adds ink", ok:
        triple_dot.width == x.width and triple_dot.height > x.height and paints(triple_dot)},
    {name: "quad-dot accent preserves advance and adds ink", ok:
        quad_dot.width == x.width and quad_dot.height > x.height and paints(quad_dot)},
    {name: "under arrows clear the descender", ok:
        all([for (mark in below) mark.width == g.width and mark.depth > g.depth and paints(mark)])},
    {name: "phantom reserves both dimensions without painting", ok:
        phantom.width == g.width and phantom.height == g.height and phantom.depth == g.depth and not paints(phantom)},
    {name: "horizontal phantom reserves only width", ok:
        horizontal.width == g.width and horizontal.height == 0 and horizontal.depth == 0 and not paints(horizontal)},
    {name: "vertical phantom reserves only vertical extent", ok:
        vertical.width == 0 and vertical.height == g.height and vertical.depth == g.depth and not paints(vertical)},
    {name: "smash paints while dropping vertical extent", ok:
        smashed.width == g.width and smashed.height == 0 and smashed.depth == 0 and paints(smashed)},
    {name: "boldsymbol selects the mathematical bold italic glyph", ok:
        bold.width == unicode_bold.width and bold.height == unicode_bold.height and bold.depth == unicode_bold.depth},
    {name: "eqref resolves and parenthesizes a custom tag", ok:
        contains(references, "href=\"#eq:value\">(Value)</a>")},
    {name: "eqref keeps unresolved references explicit", ok:
        contains(references, "latex-ref latex-unresolved") and contains(references, "(??)</a>")}
];
[for (check in checks where check.ok != true) check.name]

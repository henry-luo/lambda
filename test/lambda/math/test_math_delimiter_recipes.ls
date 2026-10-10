// TeX82 var_delimiter, fontmath.ltx and the bundled CMEX encoding; independent DVI checks live beside this test.
import math: lambda.doc.math.math
import font: lambda.doc.math.font
import svg: .mod_svg_snapshot

fn ast(source) => parse(source,{type:"math",flavor:"latex"})^
fn box(source, options = {}) => math.render_box(ast(source),options)^
fn glyphs(b) => [for (g in svg.geometry(b.element) where g.text != "") g]
fn close(a,b) => abs(a - b) < 0.00003

let variants = [for (size in ["big","Big","bigg","Bigg"]) box("\\" ++ size ++ "l(")]
let tall_source = "\\vphantom{\\rule{0pt}{60pt}}"
let parens = box("\\left(" ++ tall_source ++ "\\right)")
let brackets = box("\\left[" ++ tall_source ++ "\\right]")
let braces = box("\\left\\{" ++ tall_source ++ "\\right\\}")
let angles = box("\\left\\langle" ++ tall_source ++ "\\right\\rangle")
let paren_parts = glyphs(parens)
let bracket_parts = glyphs(brackets)
let brace_parts = glyphs(braces)
let nested = box("\\left(\\frac{1}{\\left[" ++ tall_source ++ "\\right]}\\right)")
let middle = box("\\left(" ++ tall_source ++ "\\middle|\\right)")
let aliases = [for (pair in [["(","lparen"],["[","lbrack"],["|","vert"],["<","langle"]])
    [box("\\Biggl" ++ pair[0]),box("\\Biggl\\" ++ pair[1])]]
let supplied = font.prepare(ast("\\Biggl("),{font_family:"Computer Modern Serif"})^
let scoped = glyphs(box("\\left(\\scriptstyle1\\middle|1\\right)"))
let digits = [for (g in scoped where g.text == "1") g]
let checks = [
    {name:"explicit sizes select the existing designed CMEX faces",ok:all([for (i,b in variants)
        glyphs(b)[0].node["font-family"] == "KaTeX_Size" ++ string(i + 1)])},
    // These logical extents and widths are independently emitted by tex_delimiter_conformance.test.mjs.
    {name:"largest parenthesis uses CMEX logical width and axis",ok:
        close(variants[3].width,0.7916698455810547) and close(variants[3].height,1.7500152587890625) and
        close(variants[3].depth,1.2500152587890625)},
    {name:"tall parentheses use curved tips and repeated straight sections",ok:
        paren_parts[0].text == "⎛" and paren_parts[len(paren_parts) / 2 - 1].text == "⎝" and
        paren_parts[len(paren_parts) / 2].text == "⎞" and paren_parts[len(paren_parts) - 1].text == "⎠" and
        len([for (g in paren_parts where g.text == "⎜") g]) > 1},
    {name:"tall square brackets retain both corners",ok:
        bracket_parts[0].text == "⎡" and bracket_parts[len(bracket_parts) / 2 - 1].text == "⎣" and
        bracket_parts[len(bracket_parts) / 2].text == "⎤" and bracket_parts[len(bracket_parts) - 1].text == "⎦"},
    {name:"tall braces retain exactly one middle on each side",ok:
        len([for (g in brace_parts where g.text == "⎨") g]) == 1 and
        len([for (g in brace_parts where g.text == "⎬") g]) == 1},
    {name:"finite angle variants stop at CMEX largest design",ok:
        len(glyphs(angles)) == 2 and close(angles.depth,variants[3].depth)},
    {name:"all constructed pieces retain natural proportions",ok:
        all([for (b in [*variants,parens,brackets,braces,angles,nested,middle],g in glyphs(b)) g.sx == 1.0 and g.sy == 1.0])},
    {name:"middle uses the enclosing delimiter demand",ok:
        len([for (g in glyphs(middle) where g.text == "∣") g]) > 10 and close(middle.depth,parens.depth)},
    {name:"delimiter aliases share the same construction",ok:all([for (pair in aliases)
        close(pair[0].width,pair[1].width) and close(pair[0].height,pair[1].height)])},
    {name:"middle restores enclosing math style",ok:digits[0].node["font-size"] == 700 and digits[1].node["font-size"] == 1000},
    {name:"middle normalizes binary atoms as close then open",ok:
        close(box("\\left(a+\\middle|+b\\right)").width,box("\\left(a{+}\\middle|{+}b\\right)").width)},
    {name:"supplied font profiles do not inherit bundled CMEX recipes",ok:
        supplied.delimiter_data == null and font.tex_delimiter(supplied,ord("(")) == null}
];
[for (check in checks where check.ok != true) check.name]

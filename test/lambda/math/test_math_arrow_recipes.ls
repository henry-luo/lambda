// AMS macro invariants; the independent DVI oracle verifies dimensions and positions.
import math: lambda.doc.math.math
import font: lambda.doc.math.font
import svg: .mod_svg_snapshot

fn ast(source) => parse(source,{type:"math",flavor:"latex"})^
fn box(source) => math.render_box(ast(source),{font_size:960.0 / 72.27})^
fn glyphs(b) => [for (g in svg.geometry(b.element) where g.text != "") g]
fn close(a,b) => abs(a - b) < 0.00003

let empty = box("\\xrightarrow{}")
let optional = box("\\xrightarrow[{}]{}")
let zero = box("\\xrightarrow[{{}}]{{}}")
let double_empty = box("\\xRightarrow{}")
let wide = box("\\xrightarrow{\\phantom{\\rule{30pt}{3pt}}}")
let double_wide = box("\\xLeftrightarrow{\\phantom{\\rule{30pt}{3pt}}}")
let script = box("\\scriptscriptstyle\\xrightarrow{a}")
let marked = box("\\overrightarrow{\\phantom{\\rule{3pt}{2pt}}}")
let body = box("\\phantom{\\rule{3pt}{2pt}}")
let under = box("\\underrightarrow{\\phantom{\\rule{3pt}{2pt}}}")
let after = box("\\overrightarrow{\\rule{12pt}{3pt}}^{\\rule{3pt}{4pt}}")
let braced = box("{\\overrightarrow{\\rule{12pt}{3pt}}}^{\\rule{3pt}{4pt}}")
let supplied = font.prepare(ast("\\xrightarrow{}"),{font_family:"Computer Modern Serif"})^
let checks = [
    {name:"empty optional argument is stripped once before limit presence",ok:
        close(empty.height,optional.height) and close(empty.depth,optional.depth) and
        zero.height > empty.height and zero.depth > empty.depth},
    {name:"mathtools double-arrow control spaces force both limits",ok:
        double_empty.depth > empty.depth},
    {name:"single and double shafts use glyph leaders with natural heads",ok:
        len([for (g in glyphs(wide) where g.text == "−") g]) > 2 and
        len([for (g in glyphs(double_wide) where g.text == "=") g]) > 2 and
        all([for (ch in ["⇐","⇒"]) len([for (g in glyphs(double_wide) where g.text == ch) g]) == 1])},
    {name:"arrow construction keeps matched encodings and natural glyph proportions",ok:
        all([for (b in [empty,wide,double_wide,marked,under],g in glyphs(b))
            g.node["font-family"] == "KaTeX_Main" and g.sx == 1.0 and g.sy == 1.0])},
    {name:"extensible-arrow filler retains display size inside scriptscript style",ok:
        all([for (g in glyphs(script) where contains(["−","→"],g.text)) g.node["font-size"] == 1000])},
    {name:"over and under marks retain the filler minimum and body baseline",ok:
        marked.width > body.width and close(marked.width,under.width) and
        marked.height > body.height and under.depth > body.depth},
    {name:"braces control following script attachment rather than character-accent rules",ok:
        glyphs(after)[0].y == glyphs(braced)[0].y and braced.height > after.height},
    {name:"supplied fonts cannot acquire bundled arrow companions",ok:
        supplied.delimiter_data == null and font.tex_arrow_symbol(supplied,"→","text") == null}
];
[for (check in checks where check.ok != true) check.name]

// TeX82 make_radical/make_math_accent and LaTeX r@@t; DVI oracles check exact geometry separately.
import math: lambda.doc.math.math
import font: lambda.doc.math.font
import svg: .mod_svg_snapshot

fn ast(source) => parse(source,{type:"math",flavor:"latex"})^
fn box(source, options = {}) => math.render_box(ast(source),{font_size:960.0 / 72.27,*:options})^
fn glyphs(b) => [for (g in svg.geometry(b.element) where g.text != "") g]
fn close(a,b) => abs(a - b) < 0.00003

let sizes = [for (height in [4,10,13,18,24]) box("\\sqrt{\\vphantom{\\rule{0pt}{" ++ string(height) ++ "pt}}}")]
let tall = box("\\sqrt{\\vphantom{\\rule{0pt}{60pt}}}")
let parts = glyphs(tall)
let indexed = glyphs(box("\\sqrt[3]{x}"))
let empty_degree = glyphs(box("\\sqrt[]{\\rule{10pt}{4pt}}"))
let lowered = box("\\rule[-4pt]{10pt}{10pt}")
let raised = box("\\rule[+4pt]{10pt}{10pt}")
let accents = [for (command in ["widehat","widetilde"],width in [3,11,15,30])
    box("\\" ++ command ++ "{\\phantom{\\rule{" ++ string(width) ++ "pt}{2pt}}}")]
let scripts = glyphs(box("\\scriptscriptstyle\\widehat{\\phantom{\\rule{30pt}{2pt}}}"))
let supplied = font.prepare(ast("\\sqrt{x}"),{font_family:"Computer Modern Serif"})^
let checks = [
    {name:"radicals select the original small and four finite designs",ok:all([for (i,b in sizes)
        glyphs(b)[0].node["font-family"] == if (i == 0) "KaTeX_Main" else "KaTeX_Size" ++ string(i)])},
    {name:"tall radical retains top, repeat and bottom components",ok:
        parts[0].text == "\uE001" and parts[len(parts) - 1].text == "⎷" and
        len([for (g in parts where g.text == "\uE000") g]) > 1},
    {name:"all radical and accent components keep natural proportions",ok:
        all([for (b in [*sizes,tall,*accents],g in glyphs(b)) g.sx == 1.0 and g.sy == 1.0])},
    {name:"root degree uses scriptscript text independently of the nucleus",ok:
        indexed[0].text == "3" and indexed[0].node["font-size"] == 500 and
        indexed[len(indexed) - 1].text == "x" and indexed[len(indexed) - 1].node["font-size"] == 1000},
    {name:"empty root degree retains negative TeX kern",ok:empty_degree[0].x < 0.0},
    {name:"optional signed rule raise preserves width, height and depth",ok:
        close(lowered.width,1.0) and close(lowered.height,0.6) and close(lowered.depth,0.4) and
        close(raised.width,1.0) and close(raised.height,1.4) and close(raised.depth,0.0)},
    {name:"wide accents stop at the third CMEX variant",ok:all([for (i,b in accents)
        glyphs(b)[0].node["font-family"] == "KaTeX_Size" ++ string(min(i % 4 + 1,3))])},
    {name:"wide accents keep text-sized extension fonts in scripts",ok:
        scripts[0].node["font-size"] == 1000 and scripts[0].node["font-family"] == "KaTeX_Size3"},
    {name:"supplied faces do not acquire bundled radical or accent data",ok:
        supplied.delimiter_data == null and font.tex_delimiter(supplied,ord("√")) == null and
        font.tex_accent(supplied,"widehat") == null}
];
[for (check in checks where check.ok != true) check.name]

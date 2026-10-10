import math: lambda.doc.math.math
import svg: .mod_svg_snapshot

fn box(source) => math.render_box(parse(source, 'math')^)^
fn paint(source) => svg.painted_text(box(source).element)
fn paths(source) => svg.nodes(box(source).element,'path')^
let plain = box("\\text{b}")
let raised = box("\\raisebox{1em}{b}")
let lowered = box("\\raisebox{-1em}{b}")
let checks = [
    {name:"raise moves the painted baseline", ok:abs(raised.height - plain.height - 1.0) < 0.001},
    {name:"lower adds descent", ok:abs(lowered.depth - plain.depth - 1.0) < 0.001},
    {name:"raise overrides reported extents", ok:abs(box("\\raisebox{1em}[2em][3em]{b}").height - 2.0) < 0.001 and
        abs(box("\\raisebox{1em}[2em][3em]{b}").depth - 3.0) < 0.001},
    {name:"reflection paints its argument", ok:paint("\\reflectbox{abc}") == "abc"},
    {name:"math reflection preserves fractions", ok:paint("\\mathreflectbox{x^2+\\frac{a}{b}}") == "x2+ab"},
    {name:"reflection emits a horizontal mirror", ok:contains(format(box("\\reflectbox{abc}").element,'html'),"scale(-1 1)")},
    {name:"text reflection contains embedded math", ok:paint("\\reflectbox{$x^2+\\frac{a}{b}$}") == "x2+ab"},
    {name:"vcenter retains hbox math", ok:paint("\\vcenter{\\hbox{$\\frac{a+b}{c}$}}") == "a+bc"},
    {name:"cancel draws one diagonal", ok:len(paths("\\cancel{abc}")) == 1},
    {name:"cross cancel draws both diagonals", ok:len(split(paths("\\xcancel{abc}")[0].d,"M")) == 3},
    {name:"strikeout retains text", ok:paint("\\text{\\sout{5ABC}}") == "5ABC" and len(paths("\\text{\\sout{5ABC}}")) == 1},
    {name:"phase draws an angle around its body", ok:paint("\\phase{-78^\\circ}") == "−78∘" and len(paths("\\phase{78}")) == 1}
];
[for (check in checks where check.ok != true) check.name]

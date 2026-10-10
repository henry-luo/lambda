import math: lambda.doc.math.math
import svg: .mod_svg_snapshot
import util: lambda.doc.math.util

fn ast(source) => parse(source, {type:"math", flavor:"latex"})^
fn box(source) => math.render_box(ast(source))^
fn paint(source) => svg.painted_text(box(source).element)
let choice = "\\mathchoice{D}{T}{S}{SS}"
let nested = "\\displaystyle x_{\\mathchoice{1}{2}{3}{4}_{\\mathchoice{1}{2}{3}{4}}}"
let roundtrip = util.content_items(ast(format(ast(choice), {type:"math", flavor:"latex"})^))[0]
let checks = [
    {name:"select display branch", ok:paint("\\displaystyle" ++ choice) == "D"},
    {name:"select text branch", ok:paint(choice) == "T"},
    {name:"select script branch", ok:paint("\\scriptstyle" ++ choice) == "S"},
    {name:"select scriptscript branch", ok:paint("\\scriptscriptstyle" ++ choice) == "SS"},
    {name:"nested subscripts select their own branch", ok:paint(nested) == "x34"},
    {name:"text fractions select script branches", ok:paint("\\frac{" ++ choice ++ "}{x}") == "Sx"},
    {name:"radical indices select scriptscript branches", ok:contains(paint("\\sqrt[" ++ choice ++ "]{x}"), "SS")},
    {name:"unused branches are not painted", ok:paint("\\mathchoice{\\undefined}{T}{S}{SS}") == "T"},
    {name:"chosen relation keeps its atom class", ok:box("\\mathchoice{+}{=}{-}{x}").type == "mrel"},
    {name:"display branch collects its Unicode glyph", ok:paint("\\displaystyle\\mathchoice{α}{T}{S}{SS}") == "α"},
    {name:"formatter preserves all four branches", ok:name(roundtrip) == 'mathchoice' and
        util.text_of(roundtrip.display) == "D" and util.text_of(roundtrip.text) == "T" and
        util.text_of(roundtrip.script) == "S" and util.text_of(roundtrip.scriptscript) == "SS"}
];
[for (check in checks where check.ok != true) check.name]

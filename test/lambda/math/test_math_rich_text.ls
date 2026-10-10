import math: lambda.doc.math.math
import svg: .mod_svg_snapshot

fn ast(source) => parse(source,'math')^
fn box(source) => math.render_box(ast(source))^
fn paint(source) => svg.painted_text(box(source).element)
let checks = [
    {name:"text includes math without dollar glyphs", ok:paint("\\text{for $a<b$ and $c<d$}") == "for a<b and c<d"},
    {name:"nested text styles preserve content", ok:paint("\\textsf{a \\textbf{b} \\textit{c}}") == "a b c"},
    {name:"text groups and explicit spaces", ok:paint("\\text{c~ {ab} \\ e}") == "c  ab  e"},
    {name:"text phantom suppresses content", ok:paint("\\text{a \\phantom{123}}") == "a "},
    {name:"old declarations do not paint command names", ok:paint("\\text{a\\it b\\rm c}") == "abc"},
    {name:"text accents parse their argument", ok:paint("\\text{\\'a}") == "á"},
    {name:"verbatim retains special math characters", ok:paint("\\verb!<x> & </y>!") == "<x> & </y>"},
    {name:"starred verbatim marks spaces", ok:paint("\\verb*|a b|") == "a␣b"},
    {name:"verbatim stays in its matrix cell", ok:paint("\\begin{array}{ll}\\verb!a&b!&x\\\\c&d\\end{array}") == "a&bxcd"},
    {name:"verbatim does not invoke infix fractions", ok:paint("\\verb|a\\over b|") == "a\\over b"},
    {name:"text box roundtrip retains embedded math", ok:paint(format(ast("\\raisebox{1em}{$x^2$}"),{type:"math",flavor:"latex"})^) == "x2"},
    {name:"verbatim roundtrip preserves its delimiters", ok:paint(format(ast("\\verb*|a b|"),{type:"math",flavor:"latex"})^) == "a␣b"}
];
[for (check in checks where check.ok != true) check.name]

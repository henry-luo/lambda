import math: lambda.doc.math.math
import svg: .mod_svg_snapshot

fn ast(source) => parse(source, {type:"math", flavor:"latex"})^
fn box(source) => math.render_box(ast(source))^
fn paint(source) => svg.painted_text(box(source).element)
fn close(a,b) => abs(a - b) < 0.00001
let checks = [
    {name:"kern does not consume the following variable", ok:paint("a\\kern1emb") == "ab"},
    {name:"kern preserves following scripts", ok:paint("a\\kern1emb^{c\\kern1emd}") == "abcd"},
    {name:"math kern does not consume the following variable", ok:paint("a\\mkern18mub") == "ab"},
    {name:"adjacent dimensions stop at their units", ok:paint("a\\kern1em\\kern1exb") == "ab"},
    {name:"unbraced dimension matches a braced dimension", ok:close(box("a\\kern1emb").width,box("a\\kern{1em}b").width)},
    {name:"negative dimension retains its sign", ok:close(box("a\\kern-.25emb").width,box("ab").width - 0.25)},
    {name:"space before a unit is accepted", ok:close(box("a\\kern1 emb").width,box("ab").width + 1.0)},
    {name:"physical units preserve their following variable", ok:paint("a\\kern1ptb") == "ab"}
];
[for (check in checks where check.ok != true) check.name]

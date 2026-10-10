import math: lambda.doc.math.math
import svg: .mod_svg_snapshot

fn ast(source) => parse(source,'math')^
fn box(source) => math.render_box(ast(source))^
fn paint(source) => svg.painted_text(box(source).element)
fn paths(source) => svg.nodes(box(source).element,'path')^
let cd = "\\begin{CD}A@<a<<B@>>b>C\\\\@|@AcAA@VVdV\\\\D@=E@>>>F\\end{CD}"
let arrays = "\\begin{array}{|r:c||}a&b\\\\\\hline 1\\over2&3\\\\[1ex]\\hdashline x&y\\end{array}"
let checks = [
    {name:"array infix fraction stays in its cell", ok:paint(arrays) == "ab123xy" and len(svg.nodes(box(arrays).element,'rect')) == 1},
    {name:"array solid and dashed rules survive", ok:len(paths(arrays)) == 6 and len([for (p in paths(arrays) where p["stroke-dasharray"] == "150 100") p]) == 2},
    {name:"substack creates two rows without literal commands", ok:paint("\\sum_{\\substack{a<b\\\\c<d}}") == "∑a<bc<d"},
    {name:"subarray consumes its column specification", ok:paint("\\begin{subarray}{c}a\\\\b\\end{subarray}") == "ab"},
    {name:"bracket annotations are painted", ok:paint("\\overbracket{a+b}^{\\text{note}}=\\underbracket{x+y}_{\\text{label}}") == "a+bnote=x+ylabel"},
    {name:"extensible arrows retain both labels", ok:paint("\\xRightarrow[b]{ABC}+\\xhookrightarrow[d]{EF}") == "ABCb+EFd"},
    {name:"paired harpoon emits both directions", ok:paint("\\xrightleftharpoons[b]{a}") == "⇀↽ab"},
    {name:"diagram keeps all vertices and arrow labels", ok:paint(cd) == "A←aB→bCcdDE→F"},
    {name:"diagram has vertical arrows and equality", ok:len(paths(cd)) >= 4},
    {name:"mod commands preserve their arguments", ok:paint("a\\bmod2+b\\pod{3}+c\\pmod4") == "amod2+b(3)+c(mod4)"},
    {name:"prime syntax makes superscripts", ok:paint("f'+x'''^2") == "f′+x′′′2"},
    {name:"operatorname star is a modifier", ok:paint("\\operatorname*{asin}_y x") == "asinyx"},
    {name:"equation tag retains embedded math", ok:paint("\\tag{$+$hi}x") == "(+hi)x"},
    {name:"nonprinting controls do not become text", ok:paint("a\\nonumber b\\allowbreak c\\nobreak d\\newline e") == "abcde"},
    {name:"negation wraps arbitrary atoms", ok:paint("\\not{abc}") == "abc/"},
    {name:"negated groups survive roundtrip", ok:paint(format(ast("\\not{abc}"),{type:"math",flavor:"latex"})^) == "abc/"},
    {name:"set fences survive roundtrip", ok:paint(format(ast("\\Set{x|x<1}"),{type:"math",flavor:"latex"})^) == "{x|x<1}"},
    {name:"escaped fences do not become backslashes", ok:contains(paint("\\left\\{a\\right\\}"),"{") and not contains(paint("\\left\\{a\\right\\}"),"\\")},
    {name:"double-bar escape stays distinct from a single bar", ok:paint("\\left\\|x\\right\\|") == "‖x‖" and paint("\\left|x\\right|") == "|x|"},
    {name:"missing symbol aliases resolve", ok:paint("\\mapsfrom\\pounds\\textdollar\\intop") == "↤£$∫"},
    {name:"colorbox preserves border and background", ok:len([for (r in svg.nodes(box("\\fcolorbox{blue}{red}{C}").element,'rect') where r.fill == "red" and r.stroke == "blue") r]) == 1},
    {name:"bottom smash keeps height", ok:box("\\smash[b]{y}").height == box("y").height and box("\\smash[b]{y}").depth == 0.0},
    {name:"genfrac survives roundtrip", ok:paint(format(ast("\\genfrac{}{}{2pt}{0}{a}{b}"),{type:"math",flavor:"latex"})^) == "ab"},
    {name:"diagram survives roundtrip", ok:paint(format(ast(cd),{type:"math",flavor:"latex"})^) == paint(cd)},
    {name:"actuarial angle aliases draw around their body", ok:paint("A_{\\angl n}+B_\\angln") == "An+Bn" and len(paths("\\angln")) == 1},
    {name:"a trailing rule does not add an empty array row", ok:box("\\begin{array}{c}a\\\\\\hline\\end{array}").height ==
        box("\\begin{array}{c}a\\end{array}").height}
];
[for (check in checks where check.ok != true) check.name]

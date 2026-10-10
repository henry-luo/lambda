// KaTeX screenshotter test.tex and LaTeX ltlogos.dtx define the logo's text boxes.
import math: lambda.doc.math.math
import svg: .mod_svg_snapshot

fn ast(source) => parse(source,'math')^
fn render(source) => math.render_box(ast(source))^
fn glyphs(box) => [for (g in svg.geometry(box.element) where g.text != "") g]
let logo = render("\\KaTeX")
let parts = glyphs(logo)
let variants = [for (command in ["mathbf","mathit","mathbb","mathsf","mathtt"])
    glyphs(render("\\" ++ command ++ "{\\KaTeX}"))]
let large = glyphs(render("\\large\\KaTeX"))
let bold = glyphs(render("\\textbf{\\KaTeX}"))
let scripts = [for (style in ["scriptstyle","scriptscriptstyle"])
    glyphs(render("\\" ++ style ++ "\\KaTeX"))]
let checks = [
    {name:"logo paints uppercase letters", ok:svg.painted_text(logo.element) == "KATEX"},
    {name:"A uses the reference macro's three-quarter text size", ok:len(parts) == 5 and
        parts[1].node["font-size"] == 750 and parts[0].node["font-size"] == 1000},
    {name:"logo boxes raise A and lower E", ok:len(parts) == 5 and parts[1].y < 0.0 and
        parts[3].y > 0.0 and parts[0].y == 0.0 and parts[2].y == 0.0 and parts[4].y == 0.0},
    {name:"A's top aligns with the measured T box", ok:len(parts) == 5 and
        abs(parts[1].y - render("\\text{A}").height * 750 + render("\\text{T}").height * 1000) < 0.000001},
    {name:"math alphabet commands do not change the logo's text face", ok:all([for (row in variants)
        all([for (g in row) g.node["font-family"] == "Computer Modern Serif" and
            g.node["font-style"] == "normal" and g.node["font-weight"] == 400])])},
    {name:"text font declarations still select the logo's face", ok:len(bold) == 5 and
        all([for (g in bold) g.node["font-weight"] == 700])},
    {name:"mbox logo keeps text size in math scripts", ok:all([for (row in scripts)
        len(row) == 5 and row[0].node["font-size"] == 1000 and row[1].node["font-size"] == 750])},
    {name:"large declaration sizes the logo's text boxes", ok:len(large) == 5 and
        abs(large[0].node["font-size"] - 1200) < 0.000001 and
        abs(large[1].node["font-size"] - 900) < 0.000001},
    {name:"logo command survives serialization", ok:
        svg.painted_text(render(format(ast("\\KaTeX"),{type:"math",flavor:"latex"})^).element) == "KATEX"},
    {name:"ordinary mixed-case text is preserved", ok:svg.painted_text(render("\\text{KaTeX}").element) == "KaTeX"}
];
[for (check in checks where check.ok != true) check.name]

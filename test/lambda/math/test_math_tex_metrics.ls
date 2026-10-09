// Frozen TeX82/Computer Modern reference values; regenerate with tex_reference.tex.
import math: lambda.doc.math.math
import font: lambda.doc.math.font
import svg: .mod_svg_snapshot

fn ast(source) => parse(source, {type:"math", flavor:"latex"})^
fn render(source, display = false) => math.render_box(ast(source), {display:display})^
fn close(a, b) => abs(a - b) < 0.00002
fn glyphs(box) => [for (g in svg.geometry(box.element) where g.text != "") g]
fn digit(box) => [for (g in glyphs(box) where g.text == "2") g][0]
fn rise(box) => (glyphs(box)[0].y - digit(box).y) / 1000.0

let p = font.prepare(ast("b^2"), null)^
let explicit = font.prepare(ast("b^2"), {font_family:"Computer Modern Serif"})^
let ordinary = render("b^2")
let display = render("b^2", true)
let cramped = render("\\sqrt{b^2}", true)
let cramped_base = [for (g in glyphs(cramped) where g.text == "b") g][0]
let nested = render("x^{y^2}")
let ruled = render("\\frac{a}{b}", true)
let rule = [for (g in svg.geometry(ruled.element) where name(g.node) == 'rect') g][0]
let sup_sub = render("b_M^g")
let pair = glyphs(sup_sub)
let g = [for (a in pair where a.text == "g") a][0]
let m = [for (a in pair where a.text == "M") a][0]
let g_depth = render("g").depth * 700.0
let m_height = render("M").height * 700.0
let quad = render("\\scriptstyle a\\quad b")
let no_quad = render("\\scriptstyle ab")
let sub = render("b_2")
let sum = render("\\sum_a^b", true)
let inline_sum = render("\\sum_a^b")
let sum_glyph = [for (g in glyphs(sum) where g.text == "∑") g][0]
let inline_glyph = [for (g in glyphs(inline_sum) where g.text == "∑") g][0]
let bigcup = glyphs(render("\\bigcup", true))[0]
let checks = [
    {name:"default profile retains real CMU facts", ok:p.facts.has_math == false and p.tex != null},
    {name:"explicit font does not inherit CM companion", ok:explicit.tex == null},
    {name:"text superscript rise equals TeX sup2", ok:close(rise(ordinary), 0.36289215087890625)},
    {name:"display superscript rise equals TeX sup1", ok:close(rise(display), 0.41289234161376953)},
    {name:"cramped display superscript rise equals TeX sup3", ok:close((cramped_base.y - digit(cramped).y) / 1000.0, 0.28888893127441406)},
    {name:"scripts use 10/7/5 size selection", ok:digit(ordinary).node["font-size"] == 700 and digit(nested).node["font-size"] == 500},
    {name:"single subscript uses TeX sub1", ok:close(digit(sub).y / 1000.0, 0.15)},
    {name:"CM extension rule thickness", ok:close(rule.node.height / 1000.0, 0.03999900817871094)},
    {name:"CM math axis", ok:close((0.0 - rule.y - rule.node.height / 2.0) / 1000.0, 0.25)},
    {name:"script font has its own math quad", ok:close(p.tex.styles.script.math_quad / 1000.0, 0.8194486618041992)},
    {name:"script quad stays a text em", ok:close(quad.width - no_quad.width, 1.0)},
    {name:"paired scripts clear four rule thicknesses", ok:m.y - m_height - g.y - g_depth >= 159.996 - 0.02},
    {name:"paired scripts clear four fifths x height after adjustment", ok:0.0 - g.y - g_depth >= 344.444 - 0.02},
    {name:"display operator uses its designed larger face", ok:sum_glyph.node["font-family"] == "KaTeX_Size2" and inline_glyph.node["font-family"] == "KaTeX_Size1"},
    {name:"large operator classification is shared", ok:bigcup.node["font-family"] == "KaTeX_Size2"}
];
[for (check in checks where check.ok != true) check.name]

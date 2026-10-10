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
let infinity_rows = [for (style in ["\\textstyle", "\\scriptstyle", "\\scriptscriptstyle"])
    glyphs(render(style ++ " x\\rightarrow\\infty"))]
let bracket = render("\\overbracket{a+b+c}")
let bracket_rules = svg.nodes(bracket.element,'rect')
let bracket_script = render("\\scriptstyle\\overbracket{a+b+c}")
let vertical_commands = ["uparrow","downarrow","updownarrow","Uparrow","Downarrow","Updownarrow"]
let vertical_arrows = [for (cmd in vertical_commands, i,size in ["big","Big","bigg","Bigg"])
    {level:i, box:render("\\" ++ size ++ "l\\" ++ cmd),
        script:render("\\scriptstyle\\" ++ size ++ "l\\" ++ cmd)}]
let small_arrows = render("\\left\\uparrow x\\right\\downarrow")
let tall_arrows = render("\\left\\uparrow\\vphantom{\\rule{0em}{3em}}\\right\\downarrow")
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
    {name:"large operator classification is shared", ok:bigcup.node["font-family"] == "KaTeX_Size2"},
    {name:"sizing commands consume vertical delimiter tokens", ok:all([for (cmd in vertical_commands)
        (let items = content(ast("\\bigl\\" ++ cmd)),
        len(items) == 1 and items[0].delim == "\\" ++ cmd)])},
    {name:"vertical arrows use TeX extensible delimiter heights", ok:all([for (a in vertical_arrows)
        close(a.box.height,[0.850006,1.150009,1.450012,1.750015][a.level]) and
        close(a.box.depth,[0.350006,0.650009,0.950012,1.250015][a.level])])},
    {name:"explicit big arrows retain text size inside scripts", ok:all([for (a in vertical_arrows)
        close(a.box.height,a.script.height) and close(a.box.depth,a.script.depth)])},
    {name:"arrowheads stay unscaled while TeX repeats the shaft", ok:all([for (a in vertical_arrows)
        (let parts = glyphs(a.box), len(parts) == a.level + 2 and
        all([for (g in parts) g.node["font-family"] == "KaTeX_Size1" and
            g.node["font-size"] == 1000 and g.sx == 1.0 and g.sy == 1.0]))])},
    {name:"automatic arrow delimiters first try the small CMSY character", ok:
        close(small_arrows.height,0.69445) and close(small_arrows.depth,0.19445) and
        glyphs(small_arrows)[0].node["font-family"] == "KaTeX_Main"},
    {name:"automatic tall arrows use TeX extension steps", ok:close(tall_arrows.height,3.0) and
        close(tall_arrows.depth,2.45003) and len(glyphs(tall_arrows)) == 18},
    {name:"infinity uses the bundled CM symbol instead of system fallback", ok:all([for (row in infinity_rows)
        row[2].text == "∞" and row[2].node["font-family"] == "KaTeX_Main"])},
    {name:"ordinary infinity and relation arrows retain their font baseline", ok:all([for (row in infinity_rows)
        row[2].y == row[1].y and row[1].y == row[0].y])},
    {name:"CM infinity keeps its natural advance at each math size", ok:all([for (i,row in infinity_rows)
        row[2].node["font-size"] == [1000,700,500][i]]) and close(render("\\infty").width,1.0)},
    {name:"mathtools bracket rule is the TFM braceld height", ok:close(p.tex.bracket_rule,119.99797821044922) and
        len(bracket_rules) == 3 and bracket_rules[0].height == p.tex.bracket_rule},
    {name:"mathtools bracket ends use text symbol x-height", ok:close(bracket_rules[1].height,
        0.7 * p.tex.styles.text.math_x_height)},
    {name:"mathtools brackets keep their display-style nucleus in scripts", ok:close(bracket_script.width,bracket.width) and
        close(bracket_script.height,bracket.height)},
    {name:"brackets do not invent absent TeX font parameters", ok:math.render_box(ast("\\overbracket{x}"),
        {font_family:"Computer Modern Serif"}) is error}
];
[for (check in checks where check.ok != true) check.name]

// TeX82 make_scripts/make_fraction/make_left_right and Appendix G spacing.
import math: lambda.doc.math.math
import font: lambda.doc.math.font
import sp: lambda.doc.math.spacing_table
import svg: .mod_svg_snapshot

fn ast(source) => parse(source, {type:"math", flavor:"latex"})^
fn close(a, b) => abs(a - b) < 0.02
fn render(source, options = null) => math.render_box(ast(source), options)^
fn glyphs(box) => [for (g in svg.geometry(box.element) where g.text != "") g]
fn exponent(box) => [for (g in glyphs(box) where g.text == "2") g][0]

let options = {}
let profile = font.prepare(ast("b^2+a+1"), options)^
let c = profile.tex.styles.text
let plain = render("b^2", options)
let grouped = render("{b}^2", options)
let compound = render("{bb}^2", options)
let reset = render("\\sqrt{\\textstyle b^2}", options)
let cramped = render("\\sqrt{b^2}", options)
let under = render("\\underline{b^2}", options)
let accent = render("\\hat{b}^2", options)
let bar = render("\\genfrac{}{}{2pt}{0}{a}{b}", options)
let bar_ink = [for (g in svg.geometry(bar.element) where name(g.node) == 'rect') g][0]
let bar_glyphs = glyphs(bar)
let b_height = render("b", options).height * 1000.0
let a_depth = render("a", options).depth * 1000.0
let compound_top = compound.height * 1000.0 - exponent(compound).node["font-size"] *
    render("2", options).height
let script_space = render("\\scriptstyle a\\quad b", options)
let script_no_space = render("\\scriptstyle ab", options)
let typed = render("\\scriptstyle a+b+\\displaystyle c+d", options)
let pieces = [render("\\scriptstyle a+b", options), render("c+d", options)]
let raw_plus = render("+", options)
let grouped_plus = render("{+}", options)
let spacing_string = "0234000122*4000133**3**344*0400400*000000234000111*1111112341011"
let types = ["mord", "mop", "mbin", "mrel", "mopen", "mclose", "mpunct", "minner"]
let spacing_checks = [for (i, left in types, j, right in types, style in ["display", "script"]) (
    let code = slice(spacing_string, i * 8 + j, i * 8 + j + 1),
    let expected = if (code == "*" or code == "0" or (style == "script" and code != "2")) 0.0
        else if (code == "1" or code == "2") 3.0 / 18.0 else if (code == "3") 4.0 / 18.0 else 5.0 / 18.0,
    {ok:abs(sp.get_spacing(left, right, style) - expected) < 0.000001, left:left, right:right, style:style})]
let checks = [
    {name:"single glyph uses font superscript minimum", ok:close(0.0 - exponent(plain).y, c.superscript_shift_up)},
    {name:"single grouped glyph keeps character placement", ok:close(exponent(plain).y, exponent(grouped).y)},
    {name:"compound nucleus retains height clearance", ok:compound_top >= b_height - profile.tex.styles.script.superscript_baseline_drop_max - 0.02},
    {name:"style command resets cramped flag", ok:close(glyphs(reset)[2].y - glyphs(reset)[1].y, exponent(plain).y)},
    {name:"radical uses cramped script placement", ok:close(glyphs(cramped)[2].y - glyphs(cramped)[1].y, 0.0 - c.superscript_shift_up_cramped)},
    {name:"underline keeps nucleus style", ok:close(exponent(under).y, exponent(plain).y)},
    {name:"accent scripts attach to character", ok:close(glyphs(accent)[1].y, exponent(plain).y)},
    {name:"fraction is ordinary atom", ok:render("\\frac{a}{b}", options).type == "mord"},
    {name:"grouped binary is ordinary atom", ok:grouped_plus.type == "mord" and close(raw_plus.width, grouped_plus.width)},
    {name:"explicit glue preserves binary classification", ok:close(render("a\\,+b", options).width - render("a+b", options).width, 3.0 / 18.0)},
    {name:"consecutive unary signs use normalized predecessor", ok:close(render("++b", options).width -
        (2.0 * raw_plus.width + render("b", options).width), 8.0 / 18.0)},
    {name:"all Appendix G spacing pairs", ok:len([for (check in spacing_checks where not check.ok) check]) == 0},
    {name:"quad is text em independent of math style", ok:close(script_space.width - script_no_space.width, 1.0)},
    {name:"inter-atom glue follows the current style", ok:close(typed.width - pieces[0].width - pieces[1].width - raw_plus.width * font.scale(profile, "script"), 4.0 / 18.0)},
    {name:"explicit thick fraction uses its own clearance", ok:0.0 - bar_glyphs[0].y - a_depth -
        (c.axis_height + bar_ink.node.height / 2.0) >= 3.0 * bar_ink.node.height - 0.02},
    {name:"empty genfrac thickness selects default rule", ok:len([for (g in svg.geometry(
        render("\\genfrac{}{}{}{0}{a}{b}", options).element) where name(g.node) == 'rect' and
            close(g.node.height, c.fraction_rule_thickness)) g]) == 1},
    {name:"mathop has display limits", ok:glyphs(render("\\mathop{x}_a^b", {*:options, display:true}))[1].x <
        glyphs(render("\\mathop{x}\\nolimits_a^b", {*:options, display:true}))[1].x}
];
[for (check in checks where not check.ok) check.name]

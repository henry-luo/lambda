// Math5 Phase 11: geometry must follow the supplied font resource, not a table.
import math: lambda.doc.math.math
import radiant

fn ast(source) => parse(source, {type: "math", flavor: "latex"})^
fn close(a, b) => abs(a - b) < 0.00001
fn descendants(el, tag) {
    if (not (el is element)) []
    else [*if (name(el) == tag) [el] else [], *[for (child in content(el), found in descendants(child, tag)) found]]
}

let enhanced_bytes = input("test/lambda/math/fonts/NotoSansMath-Regular.ttf", 'binary')^
let plain_bytes = input("lmd/package/latex/fonts/Serif/cmunrm.woff2", 'binary')^
let enhanced = {font_family: "FormulaMath", fonts: [{font_family: "FormulaMath", data: enhanced_bytes}], display: true}
let plain = {font_family: "FormulaOrdinary", fonts: [{font_family: "FormulaOrdinary", data: plain_bytes}], display: true}
let native = radiant.math_metrics({font_family: enhanced.font_family, font_size: 1000}, [ord("𝑥"), ord("𝑎"), ord("𝑏")], enhanced.fonts)
let x = math.render_box(ast("x"), enhanced)
let other = math.render_box(ast("x"), plain)
let again = math.render_box(ast("x"), enhanced)
let frac = math.render_box(ast("\\frac{a}{b}"), enhanced)
let rule = descendants(frac.element, 'rect')[0]
let c = native.constants
let a = native.glyphs[1]
let numerator_up = max(c.fraction_numerator_display_style_shift_up,
    c.axis_height + c.fraction_rule_thickness / 2.0 + c.fraction_num_display_style_gap_min + max(0.0, a.ink.bottom))
let nested = math.render_box(ast("\\left(\\frac{x_1^{2^3}}{\\sqrt{y}}\\right)"), enhanced)
let wide = math.render_box(ast("\\widehat{abcdefghijk}"), enhanced)
let brace = math.render_box(ast("\\overbrace{abcdefghijk}^{n}"), enhanced)
let arrow = math.render_box(ast("\\overrightarrow{AB}"), enhanced)
let matrix = math.render_box(ast("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}"), plain)
let big = math.render_box(ast("x"), {*:enhanced, font_size: 40})
let ordinary = {font_family: "Ordinary", fonts: [{font_family: "Ordinary", data: input("test/ui/svg_font_assets/rectangle.ttf", 'binary')^}]}
let checks = [
    {name: "glyph advance owns width", ok: close(x.width, native.glyphs[0].advance / 1000.0)},
    {name: "ink owns height", ok: close(x.height, 0.0 - native.glyphs[0].ink.top / 1000.0)},
    {name: "selected font changes geometry", ok: x.width != other.width and x.height != other.height},
    {name: "font contexts remain independent", ok: x.width == again.width and x.height == again.height},
    {name: "paint uses selected font text", ok: content(descendants(x.element, 'text')[0])[0] == "𝑥" and
        descendants(x.element, 'text')[0]["font-family"] == "FormulaMath" and len(descendants(x.element, 'path')) == 0},
    {name: "fraction rule comes from MATH", ok: rule.height == c.fraction_rule_thickness and rule.y == 0.0 - c.axis_height - c.fraction_rule_thickness / 2.0},
    {name: "fraction clearance comes from MATH", ok: close(frac.height, (numerator_up - a.ink.top) / 1000.0)},
    {name: "nested scripts fractions radical fences", ok: nested.width > x.width and nested.height > frac.height and nested.depth > 0},
    {name: "wide accent paints glyph construction", ok: wide.width > 4 and len(descendants(wide.element, 'path')) > 0 and len(descendants(wide.element, 'text')) == 11},
    {name: "horizontal glyph assembly", ok: brace.width > 4 and len(descendants(brace.element, 'path')) > 3 and brace.height > wide.height},
    {name: "arrow accent uses font glyphs", ok: arrow.width > 1 and len(descendants(arrow.element, 'path')) >= 1 and len(descendants(arrow.element, 'text')) == 2},
    {name: "text keeps whitespace advance", ok: math.render_box(ast("\\text{a b}"), enhanced).width > math.render_box(ast("\\text{ab}"), enhanced).width},
    {name: "second font matrix", ok: matrix.width > 1 and matrix.height > 0.5 and matrix.depth > 0},
    {name: "CSS size retains em geometry", ok: big.width == x.width and contains(big.element.style, "font-size:40px")},
    {name: "nonfinite size is rejected", ok: math.render_box(ast("x"), {*:enhanced, font_size: inf}) is error and
        math.render_box(ast("x"), {*:enhanced, font_size: nan}) is error},
    {name: "no MATH table can render covered glyph", ok: math.render_box(ast("A"), ordinary).width == 1.0},
    {name: "missing glyph is explicit error", ok: math.render_box(chr(0x10FFFF), enhanced) is error},
    {name: "default font works", ok: math.render_box(ast("x"), null).width > 0}
];
[for (check in checks where check.ok != true) check.name]

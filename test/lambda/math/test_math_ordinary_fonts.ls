// Math5: ordinary faces keep their measured metrics and SVG text font identity.
import math: lambda.doc.math.math
import stretch: lambda.doc.math.stretch
import font: lambda.doc.math.font
import bundled: lambda.doc.math.bundled
import radiant

fn ast(source) => parse(source, {type: "math", flavor: "latex"})^
fn close(a, b) => abs(a - b) < 0.00001
fn descendants(el, tag) {
    if (not (el is element)) []
    else [*if (name(el) == tag) [el] else [], *[for (child in content(el), found in descendants(child, tag)) found]]
}
let faces = [
    {font_family: "OrdinarySerif", data: input("lmd/package/latex/fonts/Serif/cmunrm.woff2", 'binary')^},
    {font_family: "OrdinarySerif", font_style: "italic", data: input("lmd/package/latex/fonts/Serif/cmunti.woff2", 'binary')^},
    {font_family: "OrdinarySerif", font_weight: 700, data: input("lmd/package/latex/fonts/Serif/cmunbx.woff2", 'binary')^},
    {font_family: "OrdinarySerif", font_weight: 700, font_style: "italic", data: input("lmd/package/latex/fonts/Serif/cmunbi.woff2", 'binary')^},
    {font_family: "OrdinarySans", data: input("lmd/package/latex/fonts/Sans/cmunss.woff2", 'binary')^}
]
let options = {font_family: "OrdinarySerif", fonts: faces, display: true}
let native = radiant.math_metrics({font_family: options.font_family, font_size: 1000}, [ord("x"), ord("(")], faces)
let italic = radiant.math_metrics({font_family: options.font_family, font_size: 1000, font_style: "italic"}, [ord("x")], faces)
let bold = radiant.math_metrics({font_family: options.font_family, font_size: 1000, font_weight: 700}, [ord("x")], faces)
let hat = radiant.math_metrics({font_family: options.font_family, font_size: 1000}, [ord("̂")], faces).glyphs[0]
let wide_hat = stretch.glyph(hat, 2000.0, false, 1.0, "mord")
let plain = math.render_box(ast("\\mathrm{x}"), options)
let variable = math.render_box(ast("x"), options)
let heavy = math.render_box(ast("\\mathbf{x}"), options)
let sans = math.render_box(ast("\\mathrm{x}"), {*:options, font_family: "OrdinarySans"})
let frac = math.render_box(ast("\\frac{x}{y}"), options)
let rule = descendants(frac.element, 'rect')[0]
let nested = math.render_box(ast("\\left(\\frac{x_1^{2^3}}{\\sqrt{y}}\\right)"), options)
let formulas = ["x_1^{2^3}", "\\frac{a+b}{c+d}", "\\sqrt[3]{\\frac{x}{y}}",
    "\\hat{x}+\\widehat{xyz}", "\\sum_{i=1}^n i^2", "\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}"]
let rendered = [for (family in ["OrdinarySerif", "OrdinarySans"], source in formulas)
    math.render_box(ast(source), {*:options, font_family: family})]
let default_ast = ast("x+\\sum_{i=1}^n i+\\mathbb{R}+\\mathcal{A}+\\mathscr{A}+\\mathfrak{A}")
let default_profile = font.prepare(default_ast, null)
let default_x = math.render_box(ast("x"))
let default_sum = font.glyph(default_profile, ord("∑"))
let default_rule = descendants(math.render_math(ast("\\frac{x}{y}")), 'rect')[0]
let default_styles = [for (style in ["double", "cal", "script", "fraktur"])
    font.character(default_profile, "A", style)]
let bundled_facts = radiant.math_metrics({font_family: bundled.SYMBOL_FAMILIES, font_size: 1000, fallback: true},
    [ord("∑"), ord("↞")], bundled.faces())
let default_rendered = [for (source in formulas) math.render_box(ast(source))]
// named symbol atoms must paint their glyphs before script sizing is compared.
let symbol_cases = [
    {source: "\\alpha", glyph: "α"}, {source: "\\beta", glyph: "β"},
    {source: "\\gamma", glyph: "γ"}, {source: "\\theta", glyph: "θ"},
    {source: "\\phi", glyph: "ϕ"}, {source: "\\psi", glyph: "ψ"},
    {source: "\\pi", glyph: "π"}, {source: "\\infty", glyph: "∞"},
    {source: "\\pm", glyph: "±"}, {source: "\\approx", glyph: "≈"}
]
let basel_source = "\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}"
let symbol_profile = font.prepare(ast(basel_source ++ " " ++ join([for (entry in symbol_cases) entry.source], " ")^), null)
let symbol_results = [for (entry in symbol_cases) {
    let box = math.render_box(ast(entry.source))
    let glyph = font.character(symbol_profile, entry.glyph, "auto");
    {source: entry.source, ok: close(box.width, glyph.advance / 1000.0) and
        content(descendants(box.element, 'text')[0])[0] == chr(glyph.codepoint) and
        descendants(box.element, 'text')[0]["font-family"] == glyph.text_family}
}]
let basel_text = descendants(math.render_display(ast(basel_source)), 'text')
let script_size = font.UNITS * font.scale(symbol_profile, "script")
let checks = [
    {name: "ordinary face has no MATH", ok: native.has_math == false and native.constants == null},
    {name: "normal advances retained", ok: close(plain.width, native.glyphs[0].advance / 1000)},
    {name: "ordinary glyph is text", ok: content(descendants(plain.element, 'text')[0])[0] == "x" and len(descendants(plain.element, 'path')) == 0},
    {name: "math italic uses ordinary italic face", ok: descendants(variable.element, 'text')[0]["font-style"] == "italic" and
        descendants(variable.element, 'text')[0]["font-family"] == "OrdinarySerif"},
    {name: "math bold uses ordinary bold face", ok: descendants(heavy.element, 'text')[0]["font-weight"] == 700},
    {name: "different ordinary font changes width", ok: sans.width != plain.width},
    {name: "fallback rule uses font underline thickness", ok: close(rule.height, native.font_metrics.underline_thickness)},
    {name: "fallback axis uses measured x height", ok: close(rule.y + rule.height / 2, 0 - native.font_metrics.x_height / 2)},
    {name: "zero-advance accent stretches its ink", ok: hat.advance == 0 and close(wide_hat.width, 2000.0) and close(wide_hat.accent, 1000.0)},
    {name: "nested formula finite", ok: nested.width > variable.width and nested.height > frac.height and nested.depth > 0},
    {name: "two ordinary fonts support math structures", ok: len([for (box in rendered where box is error or
        box.width <= 0 or box.width - box.width != 0 or box.height - box.height != 0) box]) == 0},
    {name: "installed ordinary font without snapshots", ok: math.render_box(ast("x^2+\\frac{a}{b}"), {font_family: "sans-serif"}).width > 0},
    {name: "default face needs no MATH table", ok: default_profile.facts.has_math == false and
        default_profile.family == native.font_family},
    {name: "default variable uses bundled italic text", ok: descendants(default_x.element, 'text')[0]["font-family"] == bundled.FAMILY and
        descendants(default_x.element, 'text')[0]["font-style"] == "italic" and len(descendants(default_x.element, 'style')) == 1},
    {name: "default rule uses ordinary metrics", ok: close(default_rule.height, native.font_metrics.underline_thickness)},
    {name: "default operators reuse bundled glyphs", ok: default_sum.has_math == false and
        default_sum.path == bundled_facts.glyphs[0].path and default_sum.font_family == "KaTeX_Size1" and
        bundled_facts.glyphs[1].font_family == "KaTeX_AMS"},
    {name: "default specialist alphabets keep their faces", ok: len(unique([for (g in default_styles) g.font_family])) == 4 and
        len([for (g in default_styles where g.has_math == true or len(g.path) == 0) g]) == 0},
    {name: "default structures render without a MATH font", ok: len([for (box in default_rendered where box is error or
        box.width <= 0 or box.width - box.width != 0 or box.height - box.height != 0) box]) == 0},
    {name: "parsed named symbols paint measured glyphs", ok: len([for (result in symbol_results where result.ok != true) result]) == 0},
    {name: "Basel fraction retains full-size pi and six", ok: len([for (text in basel_text where
        (content(text)[0] == "π" or content(text)[0] == "6") and text["font-size"] == font.UNITS) text]) == 2},
    {name: "Basel exponents and upper limit retain script size", ok: len([for (text in basel_text where
        (content(text)[0] == "2" or content(text)[0] == "∞") and text["font-size"] == script_size) text]) == 3},
    {name: "missing requested glyph still errors", ok: math.render_box(chr(0x10FFFF), options) is error}
];
[for (check in checks where check.ok != true) check.name]

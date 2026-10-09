// RAD07-L3: text keeps measured face/style and its font resource after serialization.
import math: lambda.doc.math.math

fn descendants(el, tag) {
    if (not (el is element)) []
    else [*if (name(el) == tag) [el] else [], *[for (child in content(el), found in descendants(child, tag)) found]]
}
fn render(source, opts = null) => math.render_math(parse(source, {type: "math", flavor: "latex"})^, opts)^
let plain = render("x+1")
let styles = render("\\mathbf{x}+\\mathit{x}+\\mathrm{x}")
let text = descendants(styles, 'text')
let css = content(descendants(styles, 'style')[0])[0]
let bytes = input("lmd/package/latex/fonts/Serif/cmunti.woff2", 'binary')^
let custom = render("x", {font_family:"Custom Math", fonts:[{font_family:"Custom Math", data:bytes}]})
let encoded = format(bytes, 'json')^
let custom_css = content(descendants(custom, 'style')[0])[0]
let serialized = format(plain, 'xml')^
let reparsed = parse(serialized, 'xml')^
let checks = [
    {name:"ordinary formula emits only text glyphs", ok:len(descendants(plain, 'text')) == 3 and len(descendants(plain, 'path')) == 0},
    {name:"style faces stay explicit", ok:text[0]["font-weight"] == 700 and text[2]["font-style"] == "italic" and text[4]["font-style"] == "normal"},
    {name:"font declarations accompany the text", ok:contains(css, "@font-face") and contains(css, "font-weight:700") and contains(css, "font-style:italic")},
    {name:"custom CSS alias survives", ok:descendants(custom, 'text')[0]["font-family"] == "Custom Math"},
    {name:"supplied font bytes survive", ok:contains(custom_css, slice(encoded, 1, len(encoded) - 1))},
    {name:"serialized SVG preserves text and fonts", ok:len(descendants(reparsed, 'text')) == 3 and len(descendants(reparsed, 'style')) == 1}
];
[for (check in checks where check.ok != true) check.name]

// D7.2.9 / RAD07-L3: text keeps measured faces and bundled file references.
import math: lambda.doc.math.math
import font_files: lambda.doc.font

fn descendants(el, tag) {
    if (not (el is element)) []
    else [*if (name(el) == tag) [el] else [], *[for (child in content(el), found in descendants(child, tag)) found]]
}
fn render(source, opts = null) => math.render_math(parse(source, {type: "math", flavor: "latex"})^, opts)^
let plain = render("x+1")
let styles = render("\\mathbf{x}+\\mathit{x}+\\mathrm{x}")
let text = descendants(styles, 'text')
let css = content(descendants(styles, 'style')[0])[0]
let bytes = input("lmd/package/doc/latex/fonts/Serif/cmunti.woff2", 'binary')^
let custom = render("x", {font_family:"Custom Math", fonts:[{font_family:"Custom Math", data:bytes}]})
let encoded = format(bytes, 'json')^
let custom_css = content(descendants(custom, 'style')[0])[0]
let linked_path = font_files.resource_path("doc/latex/fonts/Serif/cmunti.woff2")
let linked = render("x", {font_family:"Custom Math", fonts:[{font_family:"Custom Math", data:bytes, src:linked_path}]})
let linked_css = content(descendants(linked, 'style')[0])[0]
let expected_files = ["cmunrm.woff2", "cmunbx.woff2", "cmunti.woff2"]
let serialized = format(plain, 'xml')^
let reparsed = parse(serialized, 'xml')^
let checks = [
    {name:"ordinary formula emits only text glyphs", ok:len(descendants(plain, 'text')) == 3 and len(descendants(plain, 'path')) == 0},
    {name:"style faces stay explicit", ok:text[0]["font-weight"] == 700 and text[2]["font-style"] == "italic" and text[4]["font-style"] == "normal"},
    {name:"font declarations accompany the text", ok:contains(css, "@font-face") and contains(css, "font-weight:700") and contains(css, "font-style:italic")},
    {name:"bundled CSS references readable font files", ok:all([for (file in expected_files,
        let path = font_files.resource_path("doc/latex/fonts/Serif/" ++ file))
        exists(path) and contains(css, "src:url(" ++ format(path, 'json')^ ++ ")")])},
    {name:"bundled CSS contains no encoded font payload", ok:not contains(css, "base64,")},
    {name:"custom CSS alias survives", ok:descendants(custom, 'text')[0]["font-family"] == "Custom Math"},
    {name:"supplied font bytes survive", ok:contains(custom_css, slice(encoded, 1, len(encoded) - 1))},
    {name:"supplied font URL takes precedence over embedding", ok:contains(linked_css, "src:url(" ++ format(linked_path, 'json')^ ++ ")") and not contains(linked_css, "base64,")},
    {name:"serialized SVG preserves text and fonts", ok:len(descendants(reparsed, 'text')) == 3 and len(descendants(reparsed, 'style')) == 1}
];
[for (check in checks where check.ok != true) check.name]

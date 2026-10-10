import math: lambda.doc.math.math
import svg: .mod_svg_snapshot

fn ast(source) => parse(source, {type:"math", flavor:"latex"})^
fn box(source) => math.render_box(ast(source))^
fn glyphs(source) => [for (g in svg.geometry(box(source).element) where name(g.node) == 'text') g]
fn close(a,b) => abs(a - b) < 0.00001
let names = ["tiny", "scriptsize", "footnotesize", "small", "normalsize", "large", "Large", "LARGE", "huge", "Huge"]
let ems = [0.5, 0.7, 0.8, 0.9, 1.0, 1.2, 1.44, 1.728, 2.074, 2.488]
let scoped = glyphs("{\\tiny a}b{\\Huge c}d")
let reset = glyphs("\\Huge a{\\tiny b}c\\normalsize d")
let huge_script = glyphs("\\Huge x^{y_z}")
let large_script = glyphs("\\large x^{y_z}")
let script_reset = glyphs("x^{\\Huge y}")
let tiny_rule = [for (g in svg.geometry(box("\\tiny\\rule{1em}{1em}").element) where name(g.node) == 'rect') g][0]
let checks = [
    {name:"named sizes paint scaled glyphs without command text", ok:all([for (i, cmd in names) (
        let g = glyphs("\\" ++ cmd ++ " x"),
        len(g) == 1 and g[0].text == "x" and close(g[0].node["font-size"], ems[i] * 1000.0))])},
    {name:"size declarations stop at group boundaries", ok:all([for (i, size in [500.0,1000.0,2488.0,1000.0])
        close(scoped[i].node["font-size"], size)])},
    {name:"nested sizes are absolute and restore their parent", ok:all([for (i, size in [2488.0,500.0,2488.0,1000.0])
        close(reset[i].node["font-size"], size)])},
    {name:"Huge uses TeX script and scriptscript sizes", ok:all([for (i, size in [2488.0,2074.0,1728.0])
        close(huge_script[i].node["font-size"], size)])},
    {name:"large uses eight and six point scripts", ok:all([for (i, size in [1200.0,800.0,600.0])
        close(large_script[i].node["font-size"], size)])},
    {name:"explicit size in superscript resets to text style", ok:close(script_reset[1].node["font-size"],2488.0)},
    {name:"em dimensions follow the named size", ok:close(tiny_rule.node.width,500.0) and close(tiny_rule.node.height,500.0)},
    {name:"size changes retain the surrounding baseline", ok:all([for (g in scoped) close(g.y,0.0)])},
    {name:"large formula includes its full painted height", ok:box("x^{\\Huge y}").height > box("x^y").height},
    {name:"formatter retains declarations", ok:contains(format(ast("{\\tiny a}b{\\Huge c}"), {type:"math", flavor:"latex"})^,"\\Huge")}
];
[for (check in checks where check.ok != true) check.name]

import math: lambda.doc.math.math
import util: lambda.doc.math.util
import svg: .mod_svg_snapshot

fn ast(source) => parse(source, {type: "math", flavor: "latex"})^
fn box(source) => math.render_box(ast(source))^
fn glyphs(b) => [for (g in svg.geometry(b.element) where contains(["→", "←", "⇀", "↽"], g.text)) g]

let commands = ["xrightleftarrows", "xrightequilibrium", "xleftequilibrium"]
let arrows = [for (cmd in commands) box("\\" ++ cmd ++ "{}")]
let parts = [for (b in arrows) glyphs(b)]
let labelled = [for (cmd in commands) box("\\" ++ cmd ++ "{verylonglabel}")]
let shafts = [for (b in labelled) [for (g in svg.geometry(b.element)
    where name(g.node) == 'rect') g]]
let labels = box("\\xrightequilibrium[below]{verylonglabel}")
let label_glyphs = glyphs(labels)
let scripted = box("\\scriptstyle\\xrightleftarrows{}")
let aliased = math.render_box(<extended_arrow cmd:"\\xrightequilibrium", above:<group "verylonglabel">, below:<group "below">>)^
let roundtrip = util.content_items(ast(format(ast("\\xrightequilibrium[below]{above}"), {type:"math", flavor:"latex"})^))[0]
let checks = [
    {name: "reaction commands have typed upper and lower labels", ok: all([for (cmd in commands) (
        let n = util.content_items(ast("\\" ++ cmd ++ "[below]{above}"))[0],
        name(n) == 'extended_arrow' and util.text_of(n.upper) == "above" and util.text_of(n.lower) == "below")])},
    {name: "paired arrows paint opposite directions", ok: parts[0][0].text == "→" and parts[0][1].text == "←" and
        parts[0][0].y < parts[0][1].y},
    {name: "equilibria paint opposing harpoons", ok: all([for (i in [1, 2])
        parts[i][0].text == "⇀" and parts[i][1].text == "↽" and parts[i][0].y < parts[i][1].y])},
    {name: "harpoons use the same bundled face", ok: parts[1][0].node["font-family"] == "KaTeX_Main" and
        parts[1][1].node["font-family"] == "KaTeX_Main"},
    {name: "right equilibrium shortens the returning harpoon", ok: shafts[1][0].node.width > shafts[1][1].node.width},
    {name: "left equilibrium shortens the forward harpoon", ok: shafts[2][0].node.width < shafts[2][1].node.width},
    {name: "short harpoons keep half-em insets on both sides", ok:
        abs(shafts[1][1].x + shafts[1][1].node.width - labelled[1].width * 1000.0 + 500.0) < 0.01 and
        abs(shafts[2][0].x - 500.0) < 0.01},
    {name: "arrowheads retain their natural proportions", ok: all([for (row in parts, g in row) g.sx == g.sy])},
    {name: "empty labels retain a useful minimum relation width", ok: all([for (b in arrows) b.width >= 1.75 and b.type == "mrel"])},
    {name: "labels extend arrow width and clear both sides", ok: labels.width > arrows[1].width and
        labels.height > arrows[1].height and labels.depth > arrows[1].depth and len(label_glyphs) == 2},
    {name: "legacy AST label aliases retain both labels", ok:
        aliased.width == labels.width and aliased.height == labels.height and aliased.depth == labels.depth},
    {name: "script style scales reaction arrows", ok: scripted.width < arrows[0].width and len(glyphs(scripted)) == 2},
    {name: "formatter retains both arrow labels", ok: name(roundtrip) == 'extended_arrow' and
        util.text_of(roundtrip.upper) == "above" and util.text_of(roundtrip.lower) == "below"}
];
[for (check in checks where check.ok != true) check.name]

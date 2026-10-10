// Bundled TeX macro contract; pixel equality with STIX is not a layout rule.
import math: lambda.doc.math.math
import font: lambda.doc.math.font
import bundled: lambda.doc.math.bundled
import svg: .mod_svg_snapshot

fn ast(source) => parse(source,{type:"math",flavor:"latex"})^
fn render(source, options = null) => math.render_box(ast(source),options)^
fn close(a,b) => abs(a - b) < 0.00001
fn glyphs(b) => [for (g in svg.geometry(b.element) where g.text != "") g]
fn signature(b) => [for (g in glyphs(b)) [g.text,g.x,g.y,g.node["font-size"]]]
fn same(a,b) => a.type == b.type and close(a.width,b.width) and close(a.height,b.height) and close(a.depth,b.depth) and
    signature(a) == signature(b)

let styles = ["display","text","script","scriptscript"]
let references = [for (style in styles) {circle:render("\\" ++ style ++ "style\\bigcirc"),
    bases:[for (key in ["iint","iiint"]) render("\\" ++ style ++ "style\\" ++ key)]}]
let cases = [for (i,style in styles, j,key in ["oiint","oiiint"])
    {style:i,key:key,base:references[i].bases[j],circle:references[i].circle,
        box:render("\\" ++ style ++ "style\\" ++ key)}]
let profile = font.prepare(ast("\\oiint+\\oiiint"),null)^
let scripted = [for (style in ["display","text"], key in ["oiint","oiiint"])
    {base:render("\\" ++ style ++ "style\\" ++ key),
        normal:render("\\" ++ style ++ "style\\" ++ key ++ "_i^n"),
        side:render("\\" ++ style ++ "style\\" ++ key ++ "\\nolimits_i^n"),
        stacked:render("\\" ++ style ++ "style\\" ++ key ++ "\\limits_i^n")}]
let scaled = [for (size in ["small","large"], key in ["oiint","oiiint"])
    {size:size,box:render("\\" ++ size ++ "\\" ++ key)}]
let surrounded = render("a\\oiint b")
let explicit_options = {font_family:"Noto Sans Math",fonts:[{font_family:"Noto Sans Math",
    data:input("test/lambda/math/fonts/NotoSansMath-Regular.ttf",'binary')^}]}
let explicit = render("\\oiint",explicit_options)
let checks = [
    {name:"bundled resources contain only the existing CMU and KaTeX families",ok:all([for (face in bundled.faces()^)
        starts_with(face.font_family,"Computer Modern") or starts_with(face.font_family,"KaTeX_")])},
    {name:"closed composites do not query a platform glyph",ok:all([for (cp in [ord("∯"),ord("∰")])
        not contains(profile.fallback_points,cp)])},
    {name:"both overlays paint an integral and a natural circle",ok:all([for (entry in cases)
        (let painted = glyphs(entry.box),len(painted) == 2 and
        painted[0].text == (if (entry.key == "oiint") "∬" else "∭") and painted[1].text == "◯")])},
    {name:"display alone selects the existing larger integral face",ok:all([for (entry in cases)
        glyphs(entry.box)[0].node["font-family"] == (if (entry.style == 0) "KaTeX_Size2" else "KaTeX_Size1")])},
    {name:"circle stays in the existing CM symbol face",ok:all([for (entry in cases)
        glyphs(entry.box)[1].node["font-family"] == "KaTeX_Main"])},
    {name:"style sizes are 10/7/5 without outline stretching",ok:all([for (entry in cases)
        all([for (g in glyphs(entry.box)) g.node["font-size"] == [1000,1000,700,500][entry.style] and
            g.sx == 1.0 and g.sy == 1.0]) and len(svg.nodes(entry.box.element,'path')) == 0])},
    {name:"ooalign width is the maximum of its rows",ok:all([for (entry in cases)
        close(entry.box.width,max(entry.base.width,entry.circle.width))])},
    {name:"hfil centers both rows by their measured advances",ok:all([for (entry in cases)
        (let painted = glyphs(entry.box),
        close(painted[0].x,(entry.box.width - entry.base.width) * 500.0) and
        close(painted[1].x,(entry.box.width - entry.circle.width) * 500.0))])},
    {name:"vphantom preserves the union of row extents",ok:all([for (entry in cases)
        close(entry.box.height + entry.box.depth,max(entry.base.height,entry.circle.height) +
            max(entry.base.depth,entry.circle.depth))])},
    {name:"vcenter uses the current TeX math axis",ok:all([for (entry in cases)
        close((entry.box.height - entry.box.depth) * 500.0,
            profile.tex.styles[if (entry.style == 0) "text" else styles[entry.style]].axis_height)])},
    {name:"compound operators have no character italic correction",ok:all([for (entry in cases)
        entry.box.italic == 0.0])},
    {name:"default integral limits match explicit nolimits",ok:all([for (entry in scripted)
        same(entry.normal,entry.side)])},
    {name:"side scripts attach at the compound operator advance",ok:all([for (entry in scripted)
        (let painted = glyphs(entry.side),all([for (g in slice(painted,2,4))
            close(g.x,entry.base.width * 1000.0)]) and painted[2].y < 0 and painted[3].y > 0)])},
    {name:"explicit limits center scripts without another font's italic correction",ok:all([for (entry in scripted)
        (let painted = glyphs(entry.stacked),
        all([for (g in slice(painted,2,4)) close(g.x + render(g.text,{display:false}).width * 350.0,
            entry.stacked.width * 500.0)]) and entry.stacked.height > entry.normal.height)])},
    {name:"size declarations scale both existing glyphs together",ok:all([for (entry in scaled)
        all([for (g in glyphs(entry.box)) g.node["font-size"] == (if (entry.size == "small") 900 else 1200)])])},
    {name:"operator atom receives TeX thin space on either side",ok:close(surrounded.width,
        render("a").width + cases[2].box.width + render("b").width + 1.0 / 3.0)},
    {name:"literal Unicode contour integrals use the bundled composition",ok:
        same(render("∯"),cases[2].box) and same(render("∰"),cases[3].box)},
    {name:"adjacent Unicode operators retain math-list spacing and side scripts",ok:
        same(render("∯∰"),render("\\oiint\\oiiint")) and
        same(render("a∯_i^n b"),render("a\\oiint_i^n b"))},
    {name:"explicit supplied fonts retain their own closed integral glyph",ok:
        len(glyphs(explicit)) == 1 and glyphs(explicit)[0].text == "∯" and
        glyphs(explicit)[0].node["font-family"] == "Noto Sans Math"},
    {name:"serialization preserves closed integral commands",ok:
        contains(format(ast("\\oiint_i^n+\\oiiint"),{type:"math",flavor:"latex"})^,"\\oiint")}
];
[for (check in checks where check.ok != true) check.name]

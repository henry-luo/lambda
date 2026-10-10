// AMS/LaTeX command definitions; live independent box checks are in tex_conformance.test.mjs.
import math: lambda.doc.math.math
import font: lambda.doc.math.font
import svg: .mod_svg_snapshot

fn render(source, display = false, options = {}) => math.render_box(parse(source, 'math')^,
    {*:options, display:display, font_size:960.0 / 72.27})^
fn close(a,b) => abs(a - b) < 0.00001
fn same(a,b) => close(a.width,b.width) and close(a.height,b.height) and close(a.depth,b.depth)
fn glyphs(b) => [for (g in svg.geometry(b.element) where g.text != "") g]
let styles = ["display", "text", "script", "scriptscript"]
let struts = [for (style in styles) {box:render("\\" ++ style ++ "style\\mathstrut"),
    phantom:render("\\" ++ style ++ "style\\vphantom("), paren:render("\\" ++ style ++ "style(")}]
let fractions = [for (align in ["l","c","r"]) render("\\cfrac[" ++ align ++ "]{1}{12345}")]
let verbatim = render("\\verb|iiiiWWWW|")
let punctuation = glyphs(render("\\verb|`'- <>{}~! &|"))
let literal = font.prepare(parse("\\verb|\\mathbf{x}|",'math')^,{})^
let supplied = {font_family:"Noto Sans Math",fonts:[{font_family:"Noto Sans Math",
    data:input("test/lambda/math/fonts/NotoSansMath-Regular.ttf",'binary')^}]}
let checks = [
    {name:"mathstrut is the current-style parenthesis phantom", ok:all([for (entry in struts)
        same(entry.box,entry.phantom) and entry.box.width == 0.0 and
        close(entry.box.height,entry.paren.height) and close(entry.box.depth,entry.paren.depth) and
        len(glyphs(entry.box)) == 0])},
    {name:"mathstrut uses supplied-font parenthesis metrics", ok:
        same(render("\\mathstrut",false,{font_family:"Computer Modern Serif"}),
            render("\\vphantom(",false,{font_family:"Computer Modern Serif"}))},
    {name:"phantoms and smash are ordinary compound boxes",ok:
        all([for (source in ["\\mathstrut","\\phantom\\sum","\\hphantom\\sum","\\vphantom\\sum","\\smash+"])
            render(source).type == "mord"])},
    {name:"mathstrut retains adjacent binary spacing",ok:
        close(render("a\\mathstrut+b").width,render("a+b").width)},
    {name:"phantom drops its source glyph's italic correction",ok:
        render("\\phantom{f}",false,supplied).italic == 0.0},
    {name:"continued fraction alignment preserves its box", ok:
        same(fractions[0],fractions[1]) and same(fractions[1],fractions[2])},
    {name:"continued fraction numerator honors left center right", ok:
        glyphs(fractions[0])[0].x < glyphs(fractions[1])[0].x and
        glyphs(fractions[1])[0].x < glyphs(fractions[2])[0].x},
    {name:"continued fraction includes the LaTeX numerator strut", ok:
        fractions[1].height > render("\\dfrac{1}{12345}").height},
    {name:"continued fraction defaults to centered", ok:
        same(render("\\cfrac{1}{12345}"),fractions[1]) and
        glyphs(render("\\cfrac{1}{12345}"))[0].x == glyphs(fractions[1])[0].x},
    {name:"pod argument remains an unboxed math list", ok:
        close(render("\\pod{+x}").width - render("(+x)").width,8.0 / 18.0)},
    {name:"pmod keeps six mu between mod and its argument", ok:
        close(render("\\pmod{x}").width - render("\\pod{x}").width,
            render("\\mathrm{mod}").width + 6.0 / 18.0)},
    {name:"display declaration does not change AMS display mode", ok:
        close(render("\\displaystyle\\pod{x}").width,render("\\pod{x}").width) and
        close(render("\\textstyle\\pod{x}",true).width - render("\\pod{x}").width,10.0 / 18.0)},
    {name:"mod retains its argument's outer atom class", ok:
        close(render("\\mod{=}x").width - render("\\mod{=}").width - render("x").width,5.0 / 18.0)},
    {name:"big delimiter suffixes select atom classes", ok:
        render("\\bigl\\uparrow").type == "mopen" and render("\\bigr\\downarrow").type == "mclose" and
        render("\\bigm\\updownarrow").type == "mrel" and render("\\big\\Uparrow").type == "mord"},
    {name:"big relation delimiters receive relation glue", ok:
        close(render("a\\bigm\\uparrow b").width - render("a\\big\\uparrow b").width,10.0 / 18.0)},
    {name:"verbatim selects the existing typewriter face",ok:len(glyphs(verbatim)) == 8 and
        all([for (g in glyphs(verbatim)) g.node["font-family"] == "Computer Modern Typewriter"])},
    {name:"verbatim uses fixed glyph advances",ok:close(glyphs(verbatim)[1].x - glyphs(verbatim)[0].x,
        glyphs(verbatim)[5].x - glyphs(verbatim)[4].x)},
    {name:"literal commands do not request unrelated style faces",ok:
        len([for (entry in literal.style_faces where entry.style == "bold") entry]) == 0},
    {name:"verbatim matches the typewriter text command",ok:
        same(verbatim,render("\\texttt{iiiiWWWW}"))},
    {name:"literal punctuation stays in the same typewriter face",ok:len(punctuation) > 0 and
        all([for (g in punctuation) g.node["font-family"] == "Computer Modern Typewriter"])},
    {name:"continued fraction rejects an undefined extension text baseline",ok:
        math.render_box(parse("\\sixptsize\\cfrac{1}{2}",'math')^) is error}
];
[for (check in checks where check.ok != true) check.name]

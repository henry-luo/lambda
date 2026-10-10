import math: lambda.doc.math.math
import util: lambda.doc.math.util
import svg: .mod_svg_snapshot

fn ast(source) => parse(source, {type:"math", flavor:"latex"})^
fn box(source) => math.render_box(ast(source))^
fn env(key, body, args = "") => "\\begin{" ++ key ++ "}" ++ args ++ body ++ "\\end{" ++ key ++ "}"
fn glyphs(b) => [for (g in svg.geometry(b.element) where name(g.node) == 'text') g]
let centered = box(env("gathered", "a\\\\b"))
let starred = box(env("pmatrix*", "a&bbb\\\\cc&d", "[r]"))
let left = box(env("matrix*", "a\\\\bbb", "[l]"))
let alignedat = box(env("alignedat", "a&=b&c&=d\\\\aa&=e&cc&=f", "{2}"))
let aligned = box(env("aligned", "a&=b&c&=d\\\\aa&=e&cc&=f"))
let rows = glyphs(centered)
let left_rows = glyphs(left)
let starred_node = util.content_items(ast(env("pmatrix*", "a&b\\\\c&d", "[r]")))[0]
let roundtrip = util.content_items(ast(format(<math starred_node>, {type:"math", flavor:"latex"})^))[0]
let pairs = util.content_items(ast(env("alignedat", "a&=b", "{1}")))[0]
let nested_source = env("matrix", env("matrix", "a&b\\\\c&d") ++ "&e\\\\f&g")
let nested_box = box(nested_source)
let checks = [
    {name:"gathered retains two centered rows", ok:svg.painted_text(centered.element) == "ab" and rows[0].y < rows[1].y},
    {name:"AMS line struts and jot keep rows apart", ok:rows[1].y - rows[0].y >= 1499.99},
    {name:"ordinary matrix struts keep rows apart", ok:left_rows[1].y - left_rows[0].y >= 1199.99},
    {name:"starred matrices retain their alignment option", ok:name(starred_node) == 'environment' and starred_node.alignment == "r"},
    {name:"starred matrix keeps fences and row breaks", ok:svg.painted_text(starred.element) == "(abbbccd)" and starred.height + starred.depth > left.height},
    {name:"left aligned matrix shares its column origin", ok:abs(left_rows[0].x - left_rows[1].x) < 0.00001},
    {name:"alignedat consumes its pair count", ok:pairs.pairs == "1" and svg.painted_text(box(env("alignedat", "a&=b", "{1}")).element) == "a=b"},
    {name:"alignedat adds no inter-pair padding", ok:aligned.width > alignedat.width + 1.9},
    {name:"aligned right cells retain leading relation glue", ok:box(env("alignedat", "a&=b", "{1}")).width >= box("a=b").width - 0.00001},
    {name:"gathered uses display fractions", ok:box(env("gathered", "\\frac{a}{b}")).height > box(env("matrix", "\\frac{a}{b}")).height},
    {name:"alignment rows use display operators", ok:box(env("alignedat", "a&=\\sum_1^2", "{1}")).height > box(env("matrix", "a&=\\sum_1^2")).height},
    {name:"starred options survive formatting", ok:roundtrip.alignment == "r" and roundtrip.name == "pmatrix*"},
    {name:"alignedat pair count survives formatting", ok:contains(format(<math pairs>, {type:"math", flavor:"latex"})^,"{1}")},
    {name:"nested matrices keep their own closing tokens", ok:svg.painted_text(nested_box.element) == "abcdefg" and
        len([for (g in glyphs(nested_box) where g.text == "g") g]) == 1},
    {name:"trailing row terminators do not add empty rows", ok:abs(box(env("matrix", "a\\\\b\\cr")).height -
        box(env("matrix", "a\\\\b")).height) < 0.00001},
    {name:"comments cannot close a matrix", ok:svg.painted_text(box(env("matrix", "a% \\end{matrix}\n\\\\b")).element) == "ab"}
];
[for (check in checks where check.ok != true) check.name]

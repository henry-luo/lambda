import math: lambda.doc.math.math
import util: lambda.doc.math.util
import svg: .mod_svg_snapshot
import radiant

let directory = "test/lambda/math/assets/katex/website/static/img"
let source = "khan-academy.png"
fn ast(text) => parse(text, {type: "math", flavor: "latex"})^
fn box(opts) => math.render_box(ast("\\includegraphics[" ++ opts ++ "]{" ++ source ++ "}"), {base_uri: directory})^
fn image(b) => [for (g in svg.geometry(b.element) where name(g.node) == 'image') g.node][0]
fn close(a, b) => abs(a - b) < 0.00001

let bytes = input(directory ++ "/" ++ source, 'binary')^
let facts = radiant.image_metrics(bytes)
let natural = box("")
let explicit = box("height=0.8em,totalheight=0.9em,width=0.9em,alt={KA, logo}")
let deep = box("height=0.4em,totalheight=0.9em,width=0.9em")
let above = box("height=0.9em,totalheight=0.4em,width=0.9em")
let scaled = math.render_box(ast("\\includegraphics[height=12,width=24]{" ++ source ++ "}"), {base_uri: directory, font_size: 16})^
let script = math.render_box(ast("x_{\\includegraphics[height=0.8em,totalheight=0.9em]{" ++ source ++ "}}"), {base_uri: directory})^
let formula = "\\def\\logo{\\includegraphics[height=0.8em,totalheight=0.9em,width=0.9em]{" ++ source ++ "}}\\frac{A\\logo}{\\logo}"
let fraction = math.render_box(ast(formula), {base_uri: directory})^
let rows = [for (gap in ["", "[1em]"]) math.render_box(ast("\\begin{array}{l}A\\\\" ++ gap ++ "B\\end{array}"))^]
let raw = "\\includegraphics[alt={A, B},height=1em]{a_b/test-file.png}"
let roundtrip = util.content_items(ast(format(ast(raw), {type:"math", flavor:"latex"})^))[0]
let checks = [
    {name: "host reports intrinsic image facts", ok: facts.width > 0 and facts.height > 0 and facts.mime_type == "image/png"},
    {name: "invalid bytes have no invented dimensions", ok: radiant.image_metrics(b'00') == null},
    {name: "default height preserves intrinsic aspect ratio", ok: close(natural.height, 0.9) and natural.depth == 0 and
        close(natural.width, 0.9 * facts.width / facts.height)},
    {name: "zero width and totalheight keep automatic sizing", ok: close(box("width=0,totalheight=0").width, natural.width)},
    {name: "height and totalheight determine the baseline", ok: close(explicit.height, 0.8) and close(explicit.depth, 0.1) and
        close(explicit.width, 0.9) and image(explicit).y == -800 and image(explicit).height == 900},
    {name: "deep images retain the full raster", ok: close(deep.height, 0.4) and close(deep.depth, 0.5) and image(deep).height == 900},
    {name: "images may sit entirely above the baseline", ok: close(above.height, 0.9) and close(above.depth, -0.5)},
    {name: "images embed bytes and accessible alt text", ok: starts_with(image(explicit).href, "data:image/png;base64,") and
        image(explicit)["aria-label"] == "KA, logo" and image(explicit).preserveAspectRatio == "none"},
    {name: "unitless image dimensions use big points", ok: close(scaled.height, 1.0) and close(scaled.width, 2.0)},
    {name: "scripts and fractions retain actual images", ok:
        len([for (g in svg.geometry(script.element) where name(g.node) == 'image') g]) == 1 and
        len([for (g in svg.geometry(fraction.element) where name(g.node) == 'image') g]) == 2},
    {name: "row spacing reserves height without painting bracket text", ok:
        close(rows[1].height + rows[1].depth - rows[0].height - rows[0].depth, 1.0) and
        svg.painted_text(rows[1].element) == svg.painted_text(rows[0].element)},
    {name: "raw paths and option groups survive formatting", ok: roundtrip.src == "a_b/test-file.png" and
        roundtrip.options == "alt={A, B},height=1em"},
    {name: "missing images and unsupported options report errors", ok:
        math.render_box(ast("\\includegraphics{missing.png}"), {base_uri: directory}) is error and
        math.render_box(ast("\\includegraphics[angle=90]{" ++ source ++ "}"), {base_uri: directory}) is error},
    {name: "invalid image lengths report errors", ok: math.render_box(ast("\\includegraphics[height=-1em]{" ++ source ++ "}"),
        {base_uri: directory}) is error}
];
[for (check in checks where check.ok != true) check.name]

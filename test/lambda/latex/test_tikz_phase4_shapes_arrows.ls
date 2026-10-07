// Phase IV §9.6: arrows.meta tip specifications and shapes.geometric nodes.
// Tip defaults follow PGF 3.1.10 (pgflibraryarrows.meta, pgfcorearrows); anchor
// geometry is checked against TeX in test_tikz_phase4_positioning.
import tikz: lambda.doc.tikz.tikz
import named: lambda.doc.tikz.named
import arrows: lambda.doc.tikz.arrows
import shapes: lambda.doc.tikz.shapes

let PT_PX = 96.0 / 72.27

fn r3(v) => float(int(v * 1000.0 + (if (v < 0.0) -0.5 else 0.5))) / 1000.0

fn failure(source) => tikz.render(source) ^ { ^.message }

fn tip_summary(key) any^ {
    let parsed = arrows.parse_key(key)
    let tip = arrows.resolve(parsed.end, 0.4 * PT_PX)^;
    key ++ " " ++ tip.kind ++ " length=" ++ string(r3(tip.length / PT_PX)) ++
        "pt width=" ++ string(r3(tip.width / PT_PX)) ++ "pt inset=" ++
        string(r3(tip.inset / PT_PX)) ++ "pt" ++ (if (tip.hollow) " open" else "")
}

fn side(tip) => if (tip == null) "none" else tip.kind ++ (if (tip.name == null) "" else ":" ++ tip.name)

// Line width 0.4pt; values in TeX pt.
let tips = [for (key in ["-Stealth", "-{Stealth[length=3mm]}", "-{Stealth[length=2pt 3]}",
    "-{Stealth[open]}", "-{Stealth[scale=2]}", "-Latex", "-LaTeX", "-latex", "-stealth",
    "-Triangle", "-To", "-to", "-Circle", "-Bar", "-|"]) tip_summary(key)^]

let keys = [for (key in ["->", "<-", "<->", "|-|", "{Latex}-{Latex}", "-{Stealth[length=3mm]}",
    "Triangle-", ">-<", "draw", "-{Foo}", "-{Stealth[length=3mm]"]) (let parsed = arrows.parse_key(key),
    if (parsed == null) key ++ " => not an arrow key"
    else key ++ " => " ++ side(parsed.start) ++ " / " ++ side(parsed.end))]

// The stroke stops at the Stealth notch; the tip apex stays on the endpoint.
let drawn = format(tikz.render("\\begin{tikzpicture}\\draw[-{Stealth[length=3mm]}] (0,0) -- (2,0);" ++
    "\\draw[{Latex}-{Latex}] (0,1) -- (2,1);\\draw[-Triangle] (0,2) -- (2,2);" ++
    "\\end{tikzpicture}")^, 'xml')

let box = [1.0, 0.5]
fn outline(node_source) any^ {
    let node = parse("\\begin{tikzpicture}" ++ node_source ++ "\\end{tikzpicture}",
        {type: "tikz"})^[0][0]
    let geom = shapes.geometry(shapes.shape_spec(node)^, box[0], box[1]);
    geom.kind ++ " " ++ (if (geom.vertices == null) "rx=" ++ string(r3(geom.rx)) ++ " ry=" ++
        string(r3(geom.ry))
        else string(len(geom.vertices)) ++ " vertices " ++ string([for (vertex in geom.vertices)
            [r3(vertex[0]), r3(vertex[1])]])) ++
        " half=" ++ string(r3(geom.half_w)) ++ "x" ++ string(r3(geom.half_h))
}

let geometries = [for (source in ["\\node[regular polygon, regular polygon sides=6] {};",
    "\\node[star, star points=4, star point ratio=2] {};", "\\node[ellipse] {};",
    "\\node[diamond, aspect=2] {};", "\\node[trapezium, trapezium angle=45] {};",
    "\\node[trapezium, minimum height=2cm] {};", "\\node[shape=circle, minimum size=2cm] {};",
    "\\node[regular polygon, minimum width=4cm] {};"]) outline(source)^]

let polygon_svg = shapes.outline_svg(shapes.geometry({shape: "diamond", aspect: 1.0,
    min_width: 0.0, min_height: 0.0}, 1.0, 0.5), 100.0, 100.0, 10.0,
    {stroke: "black", fill: null, width: 0.6, dash: null})

let labelled_source = "\\begin{tikzpicture}\\node[draw, star] (s) {S};" ++
    "\\node[draw, trapezium, right=of s] (t) {T};\\draw[-{Latex[open]}] (s) -- (t);" ++
    "\\end{tikzpicture}"
let labelled = format(tikz.render(labelled_source)^, 'xml')
let plans = named.plans(parse(labelled_source, {type: "tikz"})^[0])^;

[for (tip in tips) tip];
[for (key in keys) key];
[contains(drawn, "M24 99.5906 L91.937 99.5906"),
    contains(drawn, "M99.5906 99.5906 L88.252 103.843 L91.937 99.5906 L88.252 95.3386 Z"),
    contains(drawn, "M30.6851 61.7953 L92.9055 61.7953"),
    contains(drawn, "M24 24 L94.6139 24"),
    contains(drawn, "M99.5906 24 L94.6139 26.8733 L94.6139 21.1267 Z")];
[for (line in geometries) line];
format(polygon_svg, 'xml');
[contains(labelled, "tikz-named-picture"),
    [for (spec in plans) spec.kind ++ " " ++ (if (spec.kind == "node")
        spec.id ++ " " ++ spec.shape.shape ++ " " ++ (if (spec.place == null) "at"
            else spec.place.own ++ "<-" ++ spec.place.target.ref ++ "." ++ spec.place.target_anchor)
        else side(spec.end_tip) ++ (if (spec.end_tip.hollow) " open" else ""))]];
[failure("\\begin{tikzpicture}\\draw[-{Foo}] (0,0) -- (1,0);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\draw[-{Stealth[bend]}] (0,0) -- (1,0);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}[>=Foo]\\draw[->] (0,0) -- (1,0);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\draw[->] (0,0) -- (0,0);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node[regular polygon, regular polygon sides=2] {A};" ++
        "\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node[star, star points=1] {A};\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node[trapezium, trapezium left angle=200] {A};" ++
        "\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node[diamond, rounded corners] {A};\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node[shape=cloud] {A};\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node[diamond, aspect=0] {A};\\end{tikzpicture}")]

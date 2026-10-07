// Phase IV §9.6: TikZ `positioning` placement and `calc` coordinates.
// Scene geometry is compared with a pinned PGF 3.1.10 reference
// (test/latex/fixtures/tikz_phase4/positioning_reference.{tex,ref}).
import tikz: lambda.doc.tikz.tikz
import named: lambda.doc.tikz.named
import coords: lambda.doc.tikz.coords
import latex: lambda.latex.latex

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

fn r3(v) => float(int(v * 1000.0 + (if (v < 0.0) -0.5 else 0.5))) / 1000.0

fn failure(source) => tikz.render(source) ^ { ^.message }

// Node boxes are 1cm x 0.5cm, the reference's \rule{1cm}{5mm} with zero sep.
fn scene_of(source) any^ {
    let picture = parse(source, {type: "tikz"})^[0]
    let specs = named.plans(picture)^
    named.scene(specs, [for (spec in specs)
        if (spec.kind == "node") [1.0, 0.5] else [0.0, 0.0]])^
}

let placed = scene_of("\\begin{tikzpicture}" ++
    "\\node (a) at (0,0) {A};" ++
    "\\node (b) [right=of a] {B};" ++
    "\\node (c) [below=2cm of a] {C};" ++
    "\\node (d) [above right=of b] {D};" ++
    "\\node (e) [below left=1cm and 2cm of a] {E};" ++
    "\\node (f) [on grid, right=of a] {F};" ++
    "\\node (g) [above right=1cm of b] {G};" ++
    "\\node (h) [circle, right=5mm of b] {H};" ++
    "\\node (k) [right=of a, on grid] {K};" ++
    "\\node (n) [above=3mm] at (0,4) {N};" ++
    "\\coordinate (m1) at ($(a)!0.5!(b)$);" ++
    "\\coordinate (m2) at ($(a)+(1,2)$);" ++
    "\\coordinate (m3) at ($(a)!1cm!(c)$);" ++
    "\\coordinate (m4) at ($(a)!0.5!90:(b)$);" ++
    "\\coordinate (m5) at ($(a)!(d)!(b)$);" ++
    "\\coordinate (m6) at ($2*(b)-(a)$);" ++
    "\\coordinate (m7) at ($(a.north east)+(0.5,0)$);" ++
    "\\coordinate (m8) at ($(a)!0.25!(b)!0.5!(c)$);" ++
    "\\coordinate (m9) at ($(a)!0.5!($(b)+(0,2)$)$);" ++
    "\\end{tikzpicture}")^

let shaped = scene_of("\\begin{tikzpicture}" ++
    "\\node[regular polygon, regular polygon sides=6] (r) at (0,0) {};" ++
    "\\node[regular polygon] (p) at (4,0) {};" ++
    "\\node[star, star points=5] (s) at (8,0) {};" ++
    "\\node[ellipse] (el) at (0,-4) {};" ++
    "\\node[diamond] (dm) at (4,-4) {};" ++
    "\\node[diamond, aspect=2] (da) at (8,-4) {};" ++
    "\\node[trapezium] (t) at (0,-8) {};" ++
    "\\node[trapezium, trapezium left angle=120, trapezium right angle=70] (t2) at (4,-8) {};" ++
    "\\node[circle] (ci) at (8,-8) {};" ++
    "\\node[regular polygon, regular polygon sides=4, minimum size=3cm] (q) at (0,-12) {};" ++
    "\\end{tikzpicture}")^

let reference_text = input("test/latex/fixtures/tikz_phase4/positioning_reference.ref", "text")^
let references = [for (line in split(reference_text, "\n")
    where trim(line) != "" and not starts_with(trim(line), "#"))
    (let parts = split(trim(line), "|"),
     {id: parts[0], anchor: parts[1], x: float(parts[2]), y: float(parts[3])})]

fn compare(reference) any^ {
    let entries = [*placed.nodes, *shaped.nodes]
    let point = coords.named_point(entries, reference.id, reference.anchor)^
    let close = abs(point[0] - reference.x) < 0.002 and abs(point[1] - reference.y) < 0.002;
    reference.id ++ "." ++ reference.anchor ++ " " ++ string(r3(point[0])) ++ " " ++
        string(r3(point[1])) ++ (if (close) " ok" else " MISMATCH")
}

// Syntax: options after the node name and calc bodies are preserved verbatim.
let syntax = parse("\\begin{tikzpicture}\\node (a) {A};\\node (b) [right=2cm of a] {B};" ++
    "\\coordinate (m) at ($(a)!0.5!(b)$);\\draw (a) -- ($(a)+(1,2)$);\\end{tikzpicture}",
    {type: "tikz"})^[0]
let syntax_nodes = children_named(syntax, "node")
let syntax_point = children_named(children_named(syntax, "path")[0], "point")[1]

// A named-node edge to a calc point stops at the node border and ends at the point.
let routed = scene_of("\\begin{tikzpicture}\\node (a) at (0,0) {A};" ++
    "\\draw (a) -- ($(a)+(0,2)$);\\draw (a.east) -| ($(a)+(2,1)$);\\end{tikzpicture}")^

// Coordinate-only pictures use the direct path renderer.
let direct = format(tikz.render("\\begin{tikzpicture}\\coordinate (A) at (0,0);" ++
    "\\coordinate (B) at (4,2);\\draw (A) -- ($(A)!0.5!(B)$) -- ($(B)+(0,-2)$);" ++
    "\\end{tikzpicture}")^, 'xml')

let document = latex.render_to_html(parse("\\documentclass{article}\\usepackage{tikz}" ++
    "\\usetikzlibrary{positioning,calc}\\begin{document}\\begin{tikzpicture}" ++
    "\\node[draw] (a) {Start};\\node[draw] (b) [right=of a] {End};" ++
    "\\draw[->] (a) -- (b);\\end{tikzpicture}\\end{document}", {type: "latex"})^,
    {standalone: false});

[for (reference in references) compare(reference)^];
[for (route in routed.edges, segment in route.segments)
    [for (point in segment) [r3(point[0]), r3(point[1])]]];
[syntax_nodes[1].id, syntax_nodes[1].options_source, string(name(syntax_nodes[1][0])),
    syntax_nodes[1][0].key, syntax_nodes[1][0].value,
    children_named(syntax, "coordinate")[0].at_calc, syntax_point.calc];
[contains(direct, "M24 61.7953 L99.5906 24 L175.181 61.7953"),
    contains(document, "tikz-named-picture"), not contains(document, "latex-tikz-unsupported")];
[failure("\\begin{tikzpicture}\\node (a) {A};\\node [right=of zz] {B};\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node (a) {A};\\node [right=of a, anchor=west] {B};" ++
        "\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node (a) {A};\\coordinate (m) at ($(a)!0.5(a)$);" ++
        "\\draw (a) -- (m);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node (a) {A};\\coordinate (m) at ($(a)*(a)$);" ++
        "\\draw (a) -- (m);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node (a) {A};\\draw (a.middle) -- (1,1);\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node (a) {A};\\node [right=1cm and 2cm and 3cm of a] {B};" ++
        "\\end{tikzpicture}"),
    failure("\\begin{tikzpicture}\\node (a) {A};\\coordinate (m) at ($(a)!0.5!(a)!1cm!(a)$);" ++
        "\\draw (a) -- (m);\\end{tikzpicture}")]

// Full-document fixtures exercise LaTeX source preservation and plot rendering.
import latex: lambda.latex.latex
import tikz: lambda.doc.tikz.tikz

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

let calibration = input("test/input/tikz_calibration_report.tex", {type: "latex"})^
let latency = input("test/input/tikz_service_latency.tex", {type: "latex"})^
let vector_note = input("test/input/tikz_vector_note.tex", {type: "latex"})^
let workflow = input("test/input/tikz_request_workflow.tex", {type: "latex"})^

let calibration_html = latex.render_to_html(calibration, {standalone: false})
let latency_html = latex.render_to_html(latency, {standalone: false})
let vector_html = latex.render_to_html(vector_note, {standalone: false})
let workflow_html = latex.render_to_html(workflow, {standalone: false})
let vector_document = children_named(vector_note, "document")[0]
let vector_figure = children_named(vector_document, "figure")[0]
let vector_island = children_named(vector_figure, "tikzpicture")[0]
let vector_picture = parse(vector_island.raw_source, {type: "tikz"})^[0]
let component_points = children_named(children_named(vector_picture, "path")[0], "point");
let workflow_document = children_named(workflow, "document")[0]
let workflow_figure = children_named(workflow_document, "figure")[0]
let workflow_island = children_named(workflow_figure, "tikzpicture")[0]
let workflow_picture = parse(workflow_island.raw_source, {type: "tikz"})^[0]
let workflow_nodes = children_named(workflow_picture, "node")
let workflow_paths = children_named(workflow_picture, "path");

[
    contains(calibration_html, "Optical Sensor Calibration"),
    contains(calibration_html, "tikz-axis"),
    contains(calibration_html, "Quadratic fit"),
    contains(calibration_html, "Reference samples"),
    contains(calibration_html, "<circle"),
    contains(calibration_html, "lm_latex"),
    not contains(calibration_html, "latex-tikz-unsupported"),
    contains(format(calibration, 'latex'), "\\addplot[blue] {0.5*x^2+0.2*x+0.1}"),
    contains(latency_html, "Checkout API Load Test"),
    contains(latency_html, "tikz-axis"),
    contains(latency_html, "99th percentile"),
    not contains(latency_html, "latex-tikz-unsupported"),
    contains(format(latency, 'latex'), "\\begin{semilogyaxis}"),
    contains(vector_html, "Resolving a Planar Displacement"),
    contains(vector_html, "tikz-picture"),
    contains(vector_html, "lm_latex"),
    not contains(vector_html, "latex-tikz-unsupported"),
    contains(vector_island.raw_source, "30mm"),
    component_points[2].x == 4.0 and component_points[2].y == 3.0,
    contains(format(vector_note, 'latex'), "\\node at (4.4cm,1.5cm)"),
    contains(workflow_html, "Request Validation Pipeline"),
    contains(workflow_html, "tikz-named-picture"),
    not contains(workflow_html, "latex-tikz-unsupported"),
    contains(workflow_html, "data-radiant-layout=\"lambda-tikz\""),
    len(workflow_nodes) == 4 and workflow_nodes[1].id == "valid",
    workflow_nodes[1].x == 3.0 and workflow_nodes[3].y == -2.0,
    len(workflow_paths) == 3,
    children_named(workflow_paths[0], "point")[0].ref == "request",
    children_named(workflow_paths[2], "point")[1].ref == "reject",
    contains(workflow_html, "Request"),
    contains(format(workflow, 'latex'), "\\draw[-{Latex}] (valid) -- (reject)"),
    contains(tikz.render("\\begin{tikzpicture}\\node (a) at (0,0) {A};" ++
        "\\draw (a) -- (missing);\\end{tikzpicture}") ^ { ^.message },
        "unknown TikZ node: missing"),
    contains(tikz.render("\\begin{tikzpicture}\\node (a) at (0,0) {A};" ++
        "\\node (a) at (1,0) {B};\\node (b) at (2,0) {C};" ++
        "\\draw (a) -- (b);\\end{tikzpicture}") ^ { ^.message },
        "duplicate TikZ node: a")
]

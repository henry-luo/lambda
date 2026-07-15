import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import paint: lambda.package.graph.transform.paint
import transform: lambda.package.graph.transform

fn source_for(value) => parse(
  "digraph G { graph [splines=\"" ++ value ++ "\"] a -> b }",
  {type: "graph", flavor: "dot"})

fn lowered(value) {
  let result = normalize.normalize(source_for(value));
  [value, result.graph["route-mode"], result.valid,
    [for (diagnostic in result.diagnostics) diagnostic.code]]
}

fn route_input(mode, use_splines = false) => {
  nodes: [{id: "a", width: 60, height: 40}, {id: "b", width: 60, height: 40}],
  edges: [{id: "ab0", from: "a", to: "b"}, {id: "ab1", from: "a", to: "b"}],
  direction: "LR", route_mode: mode, use_splines: use_splines, edge_sep: 16
}

fn routed(mode) {
  let result = layout.compute(route_input(mode));
  let layers = paint.layers(result);
  let edge = result.edges[0];
  let path = if (len(layers) > 0) layers[0].content[1] else null;
  let terminal_control = if (path != null and mode == "curved")
    contains(string(path.d), " Q " ++ string(edge.points[len(edge.points) - 2].x) ++
      " " ++ string(edge.points[len(edge.points) - 2].y) ++ " ")
    else false;
  [mode, len(edge.points), len(layers),
    if (path != null) path["data-route-kind"] else null,
    if (path != null) path["data-route-mode"] else null,
    if (path != null) contains(string(path.d), " C ") or contains(string(path.d), " Q ")
      else false,
    terminal_control]
}

let parsed^parse_error = input(
  "test/lambda/graph/graphviz/route_classes.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(parsed)
let html = transform.to_html(normalized.graph)
let invalid = normalize.normalize(source_for("zigzag"))
let legacy = layout.compute(route_input(null, true))
let explicit = layout.compute(route_input("line", true))

{
  lowered: [for (value in ["line", "false", "polyline", "ortho", "curved",
    "spline", "true", "none", ""]) lowered(value)],
  invalid: [invalid.valid, [for (value in invalid.diagnostics) value.code]],
  html: [normalized.graph["route-mode"], html["data-route-mode"],
    html["data-use-splines"], len(model.edges(normalized.graph))],
  compatibility: [legacy.edges[0].route_mode, explicit.edges[0].route_mode],
  routes: [for (mode in ["line", "polyline", "orthogonal", "curved", "none"])
    routed(mode)]
}

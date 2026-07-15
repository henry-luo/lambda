import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn by_id(values, id) => [for (value in values where value.id == id) value][0]

let source^source_error = input(
  "test/lambda/graph/graphviz/specialized_shapes.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let nodes = model.nodes(graph)
let html_nodes = children(transform.to_html(graph), "node")
let fixed = by_id(nodes, "a")
let fixed_html = [for (node in html_nodes where node["data-node-id"] == "a") node][0]
let fixed_children = model.element_children(fixed_html)
let geometry = layout.compute({
  nodes: [
    {id: "a", width: 180, height: 60, shape: "box",
      ports: [{id: "", side: "shape", offset: 0.5, x_offset: 0.4, y_offset: 0.8}]},
    {id: "b", width: 80, height: 40, shape: "box", ports: []}
  ],
  edges: [{id: "ab", from: "a", to: "b", directed: true}],
  directed: true, direction: "LR", route_mode: "line"
})
let placed_a = by_id(geometry.nodes, "a")
let route = geometry.edges[0].points

{
  valid: normalized.valid,
  roles: [for (node in nodes) [node.id, node["graphviz-shape"], node.shape,
    node["shape-family"]]],
  fixed: [fixed["fixed-size"], fixed["fixed-shape"], fixed.width, fixed.height,
    fixed_html["data-fixed-shape"], fixed_html["data-shape-width"],
    fixed_html["data-shape-height"],
    [for (child in fixed_children) string(name(child))],
    contains(string(fixed_html.style), "display:inline-grid"),
    contains(string(fixed_children[0].style), "width:96px;height:48px")],
  routing: [placed_a.width, 72,
    abs(route[0].x - (placed_a.x + 36.0)) < 0.001]
}

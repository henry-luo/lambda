import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^source_error = input(
  "test/lambda/graph/graphviz/annotations.dot", {type: "graph", flavor: "dot"})
let installed = transform.install()
let rendered = transform.render_scene(source, 640, 480)
let nodes = children(rendered.scene, "node")
let edges = children(rendered.scene, "edge")
let styled_source^styled_error = input(
  "test/lambda/graph/graphviz/content_shapes_markers.dot", {type: "graph", flavor: "dot"})
let styled_rendered = transform.render_scene(styled_source, 800, 600)
let styled_node = [for (node in children(styled_rendered.scene, "node")
  where node.id == "i") node][0]

{
  installed: installed,
  svg: contains(rendered.svg, "data-graph-role=\"graph\"") and
    contains(rendered.svg, "external") and contains(rendered.svg, "outside"),
  nodes: [for (node in nodes) [node.id, node.width > 0, node.height > 0]],
  edges: [for (edge in edges) [edge.from, edge.to]],
  styled: [styled_node.width, styled_node.height,
    abs(styled_node.width - styled_node.height) < 0.01]
}

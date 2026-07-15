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

{
  installed: installed,
  svg: contains(rendered.svg, "data-graph-role=\"graph\"") and
    contains(rendered.svg, "external") and contains(rendered.svg, "outside"),
  nodes: [for (node in nodes) [node.id, node.width > 0, node.height > 0]],
  edges: [for (edge in edges) [edge.from, edge.to]]
}

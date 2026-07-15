import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^source_error = input(
  "test/lambda/graph/graphviz/paint_styles.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let nodes = model.nodes(graph)
let edge = model.edges(graph)[0]
let html_nodes = children(transform.to_html(graph), "node")
let font_node = [for (node in html_nodes where node["data-node-id"] == "g") node][0]

{
  valid: normalized.valid,
  diagnostics: [for (value in normalized.diagnostics) [value.code, value.severity]],
  canonical: [for (node in nodes) [node.id, node.fill, node["font-color"]]],
  edge: [edge.stroke, edge["font-color"]],
  html: [for (node in html_nodes) [node["data-node-id"], node["data-fill"],
    contains(string(node.style), string(node["data-fill"]))]],
  font: [font_node["data-font-name"],
    contains(string(font_node.style), "font-family:Times New Roman,Times,serif"),
    contains(string(font_node.style), "font-weight:bold"),
    contains(string(font_node.style), "font-style:italic")]
}

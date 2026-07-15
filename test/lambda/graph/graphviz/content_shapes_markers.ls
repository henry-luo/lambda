import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import paint: lambda.package.graph.transform.paint
import transform: lambda.package.graph.transform

fn direct_children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^source_error = input(
  "test/lambda/graph/graphviz/content_shapes_markers.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let nodes = model.nodes(graph)
let edges = model.edges(graph)
let html = transform.to_html(graph)
let html_nodes = direct_children(html, "node")
let html_edges = direct_children(html, "edge")
let result = layout.compute({
  nodes: [for (node in nodes) {
    id: node.id, width: 72.0, height: 42.0, shape: node.shape
  }],
  edges: edges,
  directed: true,
  direction: "LR"
})
let layers = paint.layers(result)

{
  valid: normalized.valid,
  graph_label: graph.label,
  nodes: [for (node in nodes) [node.id, node.label, node.shape,
    node["shape-family"], node["graphviz-shape"]]],
  edges: [for (edge in edges) [edge.from, edge.to, edge.label,
    edge["arrow-tail"], edge["arrow-head"], edge["arrow-direction"]]],
  html_nodes: [for (node in html_nodes) [node["data-node-id"], node["data-label"],
    node["data-shape"], node["data-shape-family"], node["data-graphviz-shape"]]],
  html_edges: [for (edge in html_edges) [edge["data-from"], edge["data-to"],
    edge["data-marker-start"], edge["data-marker-end"]]],
  paint: [for (layer in layers, let defs = layer.content[0]) [
    layer.edge_id, defs[0]["data-marker-type"], defs[1]["data-marker-type"],
    string(name(defs[0][0])), string(name(defs[1][0]))
  ]]
}

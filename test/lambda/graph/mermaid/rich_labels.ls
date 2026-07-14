import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

let graph^error = input("test/lambda/graph/mermaid/rich_labels.mmd",
  {type: "graph", flavor: "mermaid"})
let html = transform.to_html(graph)
let nodes = model.nodes(graph)
let edges = model.edges(graph)
let measured_labels = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "node" or string(name(child)) == "edge-label") child]

{
  source: {
    nodes: [for (node in nodes) [node.id, node.label, node["label-format"]]],
    edges: [for (edge in edges) [edge.from, edge.to, edge.label, edge["label-format"]]]
  },
  measured: [for (label in measured_labels) {
    tag: name(label),
    id: if (name(label) == 'node') label["data-node-id"] else label["data-edge-id"],
    format: label["data-label-format"],
    content: [for (i in 0 to (len(label) - 1)) label[i]]
  }]
}

import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

let source^err = input("test/lambda/graph/mermaid/multi_node_length.mmd",
  {type: "graph", flavor: "mermaid"})
let source_edges = model.edges(source)
let html = transform.to_html(source)
let html_edges = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "edge") child]
let result = layout.compute({
  nodes: [for (node in model.nodes(source)) {
    id: string(node.id), width: 40.0, height: 20.0
  }],
  edges: source_edges,
  directed: true,
  direction: "TB"
})

{
  counts: [len(model.nodes(source)), len(source_edges)],
  edges: [for (edge in source_edges)
    [edge.from, edge.to, edge["min-length"]]],
  html_lengths: [for (edge in html_edges) edge["data-min-length"]],
  ranks: [for (node in result.nodes) [node.id, node.rank]]
}

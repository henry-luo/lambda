import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

let graph^err = input('./test/lambda/graph/mermaid/source_fidelity.mmd',
  {type: "graph", flavor: "mermaid"})
let edges = model.edges(graph)
let html = transform.to_html(graph)
let nodes = [for (child in model.child_items(html)
  where child is element and model.tag(child) == "node") child]
let html_edges = [for (child in model.child_items(html)
  where child is element and model.tag(child) == "edge") child]

[
  [graph[0].value, graph[1].value, html["data-node-sep"], html["data-curve"],
    html["data-use-splines"]],
  [for (edge in edges) [edge.from, edge.to, edge["source-segment-index"],
    edge["source-expansion-index"], edge["source-expansion-count"],
    edge["source-statement-start"] < edge["source-statement-end"]]],
  [for (node in nodes) [node["data-node-id"], node["data-interaction-action"],
    node["data-href"], node["data-callback"], node["data-tooltip"]]],
  [for (edge in html_edges where edge["data-edge-id"] == "e1:0")
    [edge.class, edge["data-animate"], edge["data-animation"], edge["data-curve"],
      edge["data-style-declarations"]]]
]

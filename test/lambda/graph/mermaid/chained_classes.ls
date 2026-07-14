import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

let source^err = input("test/lambda/graph/mermaid/chained_classes.mmd",
  {type: "graph", flavor: "mermaid"})
let html = transform.to_html(source)
let html_nodes = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "node") child]

{
  source_counts: [len(model.nodes(source)), len(model.edges(source))],
  edge_pairs: [for (edge in model.edges(source))
    [edge.from, edge.to, edge["arrow-end"]]],
  node_classes: [for (node in html_nodes)
    [node["data-node-id"], node.class]],
  node_shapes: [for (node in html_nodes)
    [node["data-node-id"], node["data-shape"]]]
}

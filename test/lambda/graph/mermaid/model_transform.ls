import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

let source^err = input("test/lambda/graph/mermaid/nested_subgraph.mmd",
  {type: "graph", flavor: "mermaid"})
let html = transform.to_html(source)
let html_nodes = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "node") child]
let html_edges = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "edge") child]
let styled_source^style_err = input("test/lambda/graph/mermaid/class_metadata.mmd",
  {type: "graph", flavor: "mermaid"})
let styled_html = transform.to_html(styled_source)
let styled_nodes = [for (i in 0 to (len(styled_html) - 1), let child = styled_html[i]
  where string(name(child)) == "node") child]

{
  direction: model.direction(source),
  source_counts: [len(model.nodes(source)), len(model.edges(source)), len(model.subgraphs(source))],
  html_counts: [len(html_nodes), len(html_edges)],
  node_groups: [for (node in html_nodes)
    [node["data-node-id"], node["data-subgraph-id"]]],
  edge_groups: [for (edge in html_edges)
    [edge["data-from"], edge["data-to"], edge["data-subgraph-id"]]],
  styled_classes: [for (node in styled_nodes) node.class]
}

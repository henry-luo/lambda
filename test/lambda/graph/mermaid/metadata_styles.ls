import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

let graph^err = input("test/lambda/graph/mermaid/metadata_styles.mmd",
  {type: "graph", flavor: "mermaid"})
let html = transform.to_html(graph)
let html_nodes = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "node") child]
let html_edges = [for (i in 0 to (len(html) - 1), let child = html[i]
  where string(name(child)) == "edge") child]
let assignments = model.style_assignments(graph)

{
  metadata: [model.title(graph), model.description(graph)],
  assignments: [for (assignment in assignments) [
    assignment["target-kind"], assignment.targets, assignment.declarations,
    assignment["source-start"] < assignment["source-end"]
  ]],
  html_graph: [html.role, html["aria-label"], html["aria-description"],
    html["data-graph-title"]],
  html_nodes: [for (node in html_nodes) [
    node["data-node-id"], node["data-style-declarations"]
  ]],
  html_edges: [for (edge in html_edges) [
    edge["data-edge-id"], edge["data-style-declarations"]
  ]]
}

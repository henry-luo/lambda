import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn direct_children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^source_error = input(
  "test/lambda/graph/graphviz/html_interactions.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let node = model.nodes(graph)[0]
let html = transform.to_html(graph)
let html_nodes = direct_children(html, "node")
let html_edges = direct_children(html, "edge")

{
  valid: normalized.valid,
  label: [model.label_source(node), model.label_format(node)],
  content: model.content_element(node),
  interactions: [for (entry in model.interactions(graph)) [entry.target, entry.action,
    entry.href, entry.tooltip, entry["target-window"]]],
  html_node: [html_nodes[0]["data-interaction-action"], html_nodes[0]["data-href"],
    html_nodes[0]["data-tooltip"], html_nodes[0]["data-link-target"]],
  html_edge: [html_edges[0]["data-interaction-action"], html_edges[0]["data-href"],
    html_edges[0]["data-tooltip"], html_edges[0]["data-link-target"]]
}

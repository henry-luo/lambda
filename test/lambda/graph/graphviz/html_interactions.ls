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
let table = model.content_items(node)[0]
let tbody = model.element_children(table)[0]
let rows = model.element_children(tbody)
let first_cells = model.element_children(rows[0])
let second_cells = model.element_children(rows[1])
let html = transform.to_html(graph)
let html_nodes = direct_children(html, "node")
let html_edges = direct_children(html, "edge")
let html_ports = direct_children(html, "port")

{
  valid: normalized.valid,
  label: [model.label_source(node), model.label_format(node)],
  table: [model.tag(table), table.align, len(rows)],
  row: [rows[0].align, rows[0].valign, rows[0].style],
  cells: [[first_cells[0].align, first_cells[0].valign, first_cells[0].colspan,
      first_cells[0].rowspan, first_cells[0]["data-record-port"], first_cells[0].style,
      [for (child in model.element_children(first_cells[0])) model.tag(child)]],
    [second_cells[0].align, second_cells[0].valign, second_cells[0].colspan,
      second_cells[0].rowspan],
    [second_cells[1].align, second_cells[1].colspan]],
  ports: [for (port in model.ports(node)) [port.id, port.side, port.offset]],
  interactions: [for (entry in model.interactions(graph)) [entry.target, entry.action,
    entry.href, entry.tooltip, entry["target-window"]]],
  html_node: [html_nodes[0]["data-interaction-action"], html_nodes[0]["data-href"],
    html_nodes[0]["data-tooltip"], html_nodes[0]["data-link-target"]],
  html_edge: [html_edges[0]["data-interaction-action"], html_edges[0]["data-href"],
    html_edges[0]["data-tooltip"], html_edges[0]["data-link-target"],
    html_edges[0]["data-from-port"]],
  html_ports: [for (port in html_ports) [port["data-node-id"], port["data-port-id"],
    port["data-port-offset"]]]
}

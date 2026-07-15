import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^source_error = input(
  "test/lambda/graph/graphviz/records_ports.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let nodes = model.nodes(graph)
let record = nodes[0]
let ports = model.ports(record)
let result = layout.compute({
  nodes: [for (node in nodes) {
    id: node.id, width: 120, height: 60,
    ports: if (node.id == "a") [for (port in ports) {
      id: port.id, side: port.side, offset: port.offset
    }] else []
  }],
  edges: model.edges(graph), direction: "LR", directed: true
})
let html = transform.to_html(graph)

{
  valid: normalized.valid,
  diagnostics: [for (value in normalized.diagnostics) value.code],
  content_tag: model.tag(model.content_items(record)[0]),
  ports: [for (port in ports) [port.id, port.side, port.offset]],
  html_ports: [for (port in children(html, "port")) [port["data-node-id"],
    port["data-port-id"], port["data-port-side"], port["data-port-offset"]]],
  starts: [for (edge in result.edges) [edge.from_port, edge.points[0].x, edge.points[0].y]]
}

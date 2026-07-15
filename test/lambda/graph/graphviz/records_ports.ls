import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn item_by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

let source^source_error = input(
  "test/lambda/graph/graphviz/records_ports.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let nodes = model.nodes(graph)
let record = nodes[0]
let nested = nodes[3]
let ports = model.ports(record)
let nested_ports = model.ports(nested)
let root_table = model.content_items(nested)[0]
let root_row = model.element_children(model.element_children(root_table)[0])[0]
let root_cells = model.element_children(root_row)
let vertical_table = model.element_children(root_cells[0])[0]
let vertical_rows = model.element_children(model.element_children(vertical_table)[0])
let middle_cell = model.element_children(vertical_rows[1])[0]
let horizontal_table = model.element_children(middle_cell)[0]
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
let compass_source^compass_error = parse(
  "digraph Compass { p [shape=record,label=\"<n> Named|Other\"] q r " ++
    "p:n -> q r:n -> q r:se -> q }",
  {type: "graph", flavor: "dot"})
let compass_normalized = normalize.normalize(compass_source)
let compass_graph = compass_normalized.graph
let compass_nodes = model.nodes(compass_graph)
let compass_result = layout.compute({
  nodes: [for (node in compass_nodes) {
    id: node.id, width: 100, height: 60, shape: node.shape,
    ports: [for (port in model.ports(node)) {
      id: port.id, side: port.side, offset: port.offset
    }]
  }],
  edges: model.edges(compass_graph), direction: "TB", directed: true
})
let compass_html = transform.to_html(compass_graph)
let r = item_by_id(compass_result.nodes, "r")
let named_edge = [for (edge in compass_result.edges where edge.from_port == "n") edge][0]
let north_edge = [for (edge in compass_result.edges where edge.from_compass == "n") edge][0]
let southeast_edge = [for (edge in compass_result.edges where edge.from_compass == "se") edge][0]
let measured_result = layout.from_velmts(
  {attrs: {'data-direction': "LR"}}, [
    {tag: "node", width: 120, height: 60, attrs: {'data-node-id': "measured"},
      children: [
        {tag: "table", box: {x: 10, y: 5}, width: 100, height: 50, children: [
          {tag: "tbody", box: {x: 0, y: 0}, width: 100, height: 50, children: [
            {tag: "tr", box: {x: 0, y: 0}, width: 100, height: 50, children: [
              {tag: "td", box: {x: 0, y: 0}, width: 40, height: 20,
                attrs: {'data-record-port': "top"}},
              {tag: "td", box: {x: 40, y: 20}, width: 60, height: 30,
                attrs: {'data-record-port': "bottom"}}
            ]}
          ]}
        ]}
      ]},
    {tag: "node", width: 40, height: 30, attrs: {'data-node-id': "target"}},
    {tag: "port", width: 0, height: 0,
      attrs: {'data-node-id': "measured", 'data-port-id': "top",
        'data-port-side': "auto", 'data-port-offset': "0.5"}},
    {tag: "port", width: 0, height: 0,
      attrs: {'data-node-id': "measured", 'data-port-id': "bottom",
        'data-port-side': "auto", 'data-port-offset': "0.5"}},
    {tag: "edge", width: 0, height: 0,
      attrs: {'data-edge-id': "mt", 'data-from': "measured", 'data-to': "target",
        'data-from-port': "top", 'data-directed': "true"}},
    {tag: "edge", width: 0, height: 0,
      attrs: {'data-edge-id': "mb", 'data-from': "measured", 'data-to': "target",
        'data-from-port': "bottom", 'data-directed': "true"}}
  ], null)
let measured_node = item_by_id(measured_result.nodes, "measured")

{
  valid: normalized.valid,
  diagnostics: [for (value in normalized.diagnostics) value.code],
  content_tag: model.tag(model.content_items(record)[0]),
  nested_axes: [root_table["data-record-axis"], vertical_table["data-record-axis"],
    horizontal_table["data-record-axis"]],
  ports: [for (port in ports) [port.id, port.side, port.offset]],
  nested_ports: [for (port in nested_ports) [port.id, port.side, port.offset]],
  html_ports: [for (port in children(html, "port")) [port["data-node-id"],
    port["data-port-id"], port["data-port-side"], port["data-port-offset"]]],
  starts: [for (edge in result.edges) [edge.from_port, edge.points[0].x, edge.points[0].y]],
  measured: {
    offsets: [for (port in measured_node.ports)
      [port.id, port.x_offset, port.y_offset]],
    starts: [for (edge in measured_result.edges)
      [edge.from_port, edge.points[0].x, edge.points[0].y]]
  },
  compass: {
    valid: compass_normalized.valid,
    canonical: [for (edge in model.edges(compass_graph))
      [edge.from, edge["from-port"], edge["from-compass"]]],
    html: [for (edge in children(compass_html, "edge"))
      [edge["data-from"], edge["data-from-port"], edge["data-from-compass"]]],
    named_port: named_edge.from_compass == null,
    north: abs(north_edge.points[0].x - r.x) < 0.001 and
      abs(north_edge.points[0].y - (r.y - r.height / 2.0)) < 0.001,
    southeast: southeast_edge.points[0].x > r.x and southeast_edge.points[0].y > r.y
  }
}

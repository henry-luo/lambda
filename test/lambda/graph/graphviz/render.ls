import model: lambda.package.graph.model
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn item_by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

fn first_route_point(edge) {
  let route = children(edge, "route")[0];
  children(route, "point")[0]
}

let source^source_error = input(
  "test/lambda/graph/graphviz/annotations.dot", {type: "graph", flavor: "dot"})
let installed = transform.install()
let rendered = transform.render_scene(source, 640, 480)
let nodes = children(rendered.scene, "node")
let edges = children(rendered.scene, "edge")
let styled_source^styled_error = input(
  "test/lambda/graph/graphviz/content_shapes_markers.dot", {type: "graph", flavor: "dot"})
let styled_rendered = transform.render_scene(styled_source, 800, 600)
let styled_node = [for (node in children(styled_rendered.scene, "node")
  where node.id == "i") node][0]
let record_source^record_error = input(
  "test/lambda/graph/graphviz/records_ports.dot", {type: "graph", flavor: "dot"})
let record_rendered = transform.render_scene(record_source, 800, 600)
let record_node = item_by_id(children(record_rendered.scene, "node"), "a")
let record_edges = children(record_rendered.scene, "edge")
let west_start = first_route_point(record_edges[0])
let east_start = first_route_point(record_edges[1])
let west_offset = (west_start.x - record_node.x) / record_node.width
let east_offset = (east_start.x - record_node.x) / record_node.width

{
  installed: installed,
  svg: contains(rendered.svg, "data-graph-role=\"graph\"") and
    contains(rendered.svg, "external") and contains(rendered.svg, "outside"),
  nodes: [for (node in nodes) [node.id, node.width > 0, node.height > 0]],
  edges: [for (edge in edges) [edge.from, edge.to]],
  styled: [styled_node.width, styled_node.height,
    abs(styled_node.width - styled_node.height) < 0.01],
  records: [len(record_edges) == 2,
    abs(west_start.y - (record_node.y + record_node.height)) < 0.01,
    abs(east_start.y - (record_node.y + record_node.height)) < 0.01,
    west_offset < 0.22, east_offset > 0.72,
    east_start.x - west_start.x > record_node.width / 2.0]
}

import model: lambda.graph.model
import transform: lambda.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where ((model.tag(child) == wanted) ^ { false })) child
]

fn item_by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

fn first_route_point(edge) any^ {
  let route = children(edge, "route")[0];
  children(route, "point")[0]
}

let source = (input(
  "test/lambda/graph/graphviz/annotations.dot", {type: "graph", flavor: "dot"})) ^ { null }
let rendered = transform.render_scene(source, 640, 480)
let nodes = children(rendered.scene, "node")
let edges = children(rendered.scene, "edge")
let styled_source = (input(
  "test/lambda/graph/graphviz/content_shapes_markers.dot", {type: "graph", flavor: "dot"})) ^ { null }
let styled_rendered = transform.render_scene(styled_source, 800, 600)
let styled_node = [for (node in ((children(styled_rendered.scene, "node")) ^ { [] })
  where node.id == "i") node][0]
let record_source = (input(
  "test/lambda/graph/graphviz/records_ports.dot", {type: "graph", flavor: "dot"})) ^ { null }
let record_rendered = transform.render_scene(record_source, 800, 600)
let record_node = item_by_id(children(record_rendered.scene, "node"), "a")
let record_edges = children(record_rendered.scene, "edge")
let west_start = first_route_point(record_edges[0])
let east_start = first_route_point(record_edges[1])
let west_offset = (west_start.y - record_node.y) / record_node.height
let east_offset = (east_start.y - record_node.y) / record_node.height
let fig6_source = (input("test/lambda/graph/upstream/dot/fig6.gv",
  {type:"graph", flavor:"dot"})) ^ { null }
let fig6 = transform.render_scene(fig6_source, 1800, 1200)
let fig6_edges = [for (edge in ((children(fig6.scene, "edge")) ^ { [] })
  where edge.from == "42" and edge.to == "4") edge]
let fig6_route = if (len(fig6_edges) > 0) children(fig6_edges[0], "route")[0] else null
let clust_source = (input("test/lambda/graph/upstream/dot/clust1.gv",
  {type:"graph", flavor:"dot"})) ^ { null }
let clust = transform.render_scene(clust_source, 800, 800)
let return_edges = [for (edge in ((children(clust.scene, "edge")) ^ { [] })
  where edge.from == "a3" and edge.to == "a0") edge]
let return_route = if (len(return_edges) > 0) children(return_edges[0], "route")[0] else null
let return_points = if (return_route != null) children(return_route, "point") else []

{
  svg: contains(rendered.svg, "data-graph-role=\"graph\"") and
    contains(rendered.svg, "external") and contains(rendered.svg, "outside"),
  nodes: [for (node in nodes) [node.id, node.width > 0, node.height > 0]],
  edges: [for (edge in edges) [edge.from, edge.to]],
  fig6_smooth_bypass: fig6_route != null and len(children(fig6_route, "point")) == 3,
  return_tangent: len(return_points) == 4 and
    abs(return_points[3].y - return_points[2].y) > 2.0 * abs(return_points[3].x - return_points[2].x),
  styled: [styled_node.width, styled_node.height,
    abs(styled_node.width - styled_node.height) < 0.01],
  records: [len(record_edges) == 2,
    abs(west_start.x - (record_node.x + record_node.width)) < 0.01,
    abs(east_start.x - (record_node.x + record_node.width)) < 0.01,
    east_start.y > west_start.y,
    west_offset < 0.3, east_offset > 0.7,
    abs(east_start.y - west_start.y) > record_node.height / 5.0]
}

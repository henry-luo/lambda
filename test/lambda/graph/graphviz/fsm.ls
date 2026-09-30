import layout: lambda.graph.layout
import model: lambda.graph.model
import normalize: lambda.graph.normalize
import route_geometry: lambda.graph.route_geometry
import transform: lambda.graph.transform

fn by_id(items, id) {
  let found = [for (item in items where item.id == id) item];
  if (len(found) > 0) found[0] else null
}

fn by_endpoints(items, from, to) {
  let found = [for (item in items where item.from == from and item.to == to) item];
  if (len(found) > 0) found[0] else null
}

fn top(points) => min([for (point in points) point.y])
fn bottom(points) => max([for (point in points) point.y])

let source = (input("test/lambda/graph/graphviz/fsm.dot",
  {type: "graph", flavor: "dot"})) ^ { null }
let normalized = normalize.normalize(source)
let graph = normalized.graph
let routed = layout.compute({
  nodes: [for (node in model.nodes(graph))
    {id: node.id, width: 40, height: 40, shape: "circle"}],
  edges: model.edges(graph), direction: "LR", route_mode: "curved",
  loop_side: "top",
  node_sep: 60, rank_sep: 80
})
let node5 = by_id(routed.nodes, "5")
let node6 = by_id(routed.nodes, "6")
let node7 = by_id(routed.nodes, "7")
let node8 = by_id(routed.nodes, "8")
let loop5 = by_endpoints(routed.edges, "5", "5")
let loop6 = by_endpoints(routed.edges, "6", "6")
let back75 = by_endpoints(routed.edges, "7", "5")
let back86 = by_endpoints(routed.edges, "8", "6")
let back85 = by_endpoints(routed.edges, "8", "5")
let long25 = by_endpoints(routed.edges, "2", "5")
let html = transform.to_html(graph)
let circle_html = [for (child in model.element_children(html)
  where child["data-node-id"] == "2") child][0]
let curve_probe = {route_mode: "curved", points: [
  {x: 0, y: 0}, {x: 10, y: -10}, {x: 20, y: 0}, {x: 30, y: 0}
]}
let fsm_children = [
  for (i, node in model.nodes(graph))
    {tag: "node", index: i, width: 40, height: 40,
      attrs: {'data-node-id': node.id, 'data-shape': node.shape}},
  for (i, edge in model.edges(graph))
    {tag: "edge-label", index: 100 + i, width: 40, height: 30,
      attrs: {'data-edge-id': edge.id, 'data-label-placement': "above"}}
]
let placed = layout.from_velmts(
  {attrs: {'data-graph-flavor': "dot", 'data-direction': "LR"}},
  fsm_children, null, {edges: model.edges(graph)})
let loop6_index = [for (i, edge in model.edges(graph)
  where edge.from == "6" and edge.to == "6") 100 + i][0]
let placed_loop6 = [for (entry in placed.placements
  where entry.index == loop6_index) entry][0]
let placed_node6 = by_id(placed.nodes, "6")
let measured = layout.from_velmts(
  {attrs: {'data-graph-flavor': "dot", 'data-direction': "LR",
    'data-rank-sep': "80"}}, [
    {tag: "node", index: 0, width: 40, height: 40,
      attrs: {'data-node-id': "a"}},
    {tag: "node", index: 1, width: 40, height: 40,
      attrs: {'data-node-id': "b"}},
    {tag: "edge-label", index: 2, width: 70, height: 18,
      attrs: {'data-edge-id': "ab"}},
    {tag: "edge", index: 3, width: 0, height: 0,
      attrs: {'data-edge-id': "ab", 'data-from': "a", 'data-to': "b"}}
  ], null)

{
  valid: normalized.valid,
  counts: [len(model.nodes(graph)), len(model.edges(graph)), len(routed.edges)],
  flavor: html["data-graph-flavor"],
  loops_above: [top(loop5.points) < node5.y - node5.height / 2.0,
    top(loop6.points) < node6.y - node6.height / 2.0],
  feedback_levels: [node7.y > node5.y, node8.y < node5.y],
  returns: [bottom(back75.points) > node7.y + node7.height / 2.0,
    top(back86.points) < top(back85.points),
    len(back85.points) == 2],
  long_edge_avoids_loop: top(long25.points) < top(loop6.points) or
    bottom(long25.points) > bottom(loop6.points),
  circle_style: contains(string(circle_html.style), "display:inline-flex") and
    contains(string(circle_html.style), "aspect-ratio:1"),
  curved_terminal: ends_with(route_geometry.path_data(curve_probe),
    "Q 20 0 30 0"),
  curve_samples: len(route_geometry.sample_points(curve_probe)) >= 5,
  loop_label_near_node: abs(placed_loop6.x + 20.0 - placed_node6.x) <= 30.0,
  measured_label_gap: measured.nodes[1].x - measured.nodes[0].x >= 158
}

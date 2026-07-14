import layout: lambda.package.graph.layout
import normalize: lambda.package.graph.normalize
import paint: lambda.package.graph.transform.paint
import transform: lambda.package.graph.transform

fn item_by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

fn placement_by_index(items, index) {
  let matches = [for (item in items where item.index == index) item];
  if (len(matches) > 0) matches[0] else null
}

fn children_named(value, tag) => [
  for (i in 0 to (len(value) - 1), let child = value[i]
    where child is element and string(name(child)) == tag) child
]

fn contains_node(cluster, node) =>
  cluster.x <= node.x - node.width / 2.0 and
  cluster.y <= node.y - node.height / 2.0 and
  cluster.x + cluster.width >= node.x + node.width / 2.0 and
  cluster.y + cluster.height >= node.y + node.height / 2.0

fn contains_cluster(outer, inner) =>
  outer.x <= inner.x and outer.y <= inner.y and
  outer.x + outer.width >= inner.x + inner.width and
  outer.y + outer.height >= inner.y + inner.height

fn points_differ(left, right) =>
  len(left) != len(right) or not all([
    for (i, point in left where i < len(right))
      abs(point.x - right[i].x) < 0.001 and abs(point.y - right[i].y) < 0.001
  ])

let authored = <graph direction: "LR";
  <subgraph id: "outer", label: "Outer", padding: 18, fill: "#f7f9fb", z: -4;
    <node id: "a", shape: "hexagon";
      "A"
      <port id: "out", side: "east", offset: 0.25>
    >
    <subgraph id: "inner", label: "Inner", padding: 12, radius: 3, z: -3;
      <node id: "b", shape: "trapezoid";
        "B"
        <port id: "out", side: "right", offset: 0.75>
      >
    >
  >
  <node id: "c", shape: "asymmetric";
    "C"
    <port id: "in", side: "west", offset: 0.5>
  >
  <edge id: "bc", from: "b", to: "c", 'from-port': "out", 'to-port': "in">
>

let canonical = normalize.normalize(authored)
let html = transform.to_html(canonical.graph)
let html_clusters = children_named(html, "cluster")
let html_cluster_labels = children_named(html, "cluster-label")
let html_ports = children_named(html, "port")
let html_nodes = children_named(html, "node")
let html_edges = children_named(html, "edge")
let hex_nodes = [for (node in html_nodes where node["data-node-id"] == "a") node]

let result = layout.compute({
  nodes: [
    {id: "a", width: 80, height: 50, shape: "hexagon", group: "outer",
      ports: [{id: "out", side: "east", offset: 0.25}]},
    {id: "b", width: 90, height: 60, shape: "trapezoid", group: "inner",
      ports: [{id: "out", side: "east", offset: 0.75}]},
    {id: "c", width: 70, height: 50, shape: "asymmetric",
      ports: [{id: "in", side: "west", offset: 0.5}]},
    {id: "d", width: 60, height: 40, shape: "trapezoid-alt"}
  ],
  edges: [
    {id: "ab", from: "a", to: "b"},
    {id: "bc1", from: "b", to: "c", from_port: "out", to_port: "in"},
    {id: "bc2", from: "b", to: "c", from_port: "out", to_port: "in"},
    {id: "ad", from: "a", to: "d"}
  ],
  clusters: [
    {id: "outer", padding: 18, label_width: 52, label_height: 18,
      label_gap: 6, fill: "#f7f9fb", stroke: "#59636e", z: -4},
    {id: "inner", parent: "outer", padding: 12, label_width: 44,
      label_height: 16, label_gap: 4, fill: "none", stroke: "#8a949e", z: -3}
  ]
}, {direction: "LR", rank_sep: 100, node_sep: 50, edge_sep: 14})

let outer = item_by_id(result.clusters, "outer")
let inner = item_by_id(result.clusters, "inner")
let a = item_by_id(result.nodes, "a")
let b = item_by_id(result.nodes, "b")
let c = item_by_id(result.nodes, "c")
let bc1 = item_by_id(result.edges, "bc1")
let bc2 = item_by_id(result.edges, "bc2")
let start = bc1.points[0]
let finish = bc1.points[len(bc1.points) - 1]
let layers = paint.layers(result)
let cluster_layers = [for (layer in layers where layer.cluster_id != null) layer]

let port_routes = layout.compute({
  nodes: [
    {id: "p", width: 80, height: 40, ports: [
      {id: "east", side: "east", offset: 0.2},
      {id: "west", side: "west", offset: 0.8}
    ]},
    {id: "q", width: 60, height: 50, ports: [
      {id: "west", side: "west", offset: 0.6}
    ]}
  ],
  edges: [
    {id: "pq1", from: "p", to: "q", from_port: "east", to_port: "west"},
    {id: "pq2", from: "p", to: "q", from_port: "east", to_port: "west"},
    {id: "loop", from: "p", to: "p", from_port: "east", to_port: "west"}
  ]
}, {direction: "LR", rank_sep: 90, edge_sep: 12})
let p = item_by_id(port_routes.nodes, "p")
let q = item_by_id(port_routes.nodes, "q")
let pq1 = item_by_id(port_routes.edges, "pq1")
let loop = item_by_id(port_routes.edges, "loop")
let pq_start = pq1.points[0]
let pq_finish = pq1.points[len(pq1.points) - 1]
let loop_start = loop.points[0]
let loop_finish = loop.points[len(loop.points) - 1]

let velmt_result = layout.from_velmts(
  {attrs: {'data-direction': "LR", 'data-rank-sep': "90"}},
  [
    {tag: "cluster", index: 0, width: 0, height: 0,
      attrs: {'data-cluster-id': "services", 'data-cluster-padding': "14",
        'data-cluster-fill': "#f7f9fb", 'data-z': "-3"}},
    {tag: "cluster-label", index: 1, width: 58, height: 18,
      attrs: {'data-cluster-id': "services", 'data-z': "1"}},
    {tag: "node", index: 2, width: 70, height: 40,
      attrs: {'data-node-id': "service", 'data-subgraph-id': "services",
        'data-shape': "hexagon"}},
    {tag: "port", index: 3, width: 0, height: 0,
      attrs: {'data-node-id': "service", 'data-port-id': "out",
        'data-port-side': "east", 'data-port-offset': "0.3"}},
    {tag: "node", index: 4, width: 60, height: 40,
      attrs: {'data-node-id': "client", 'data-shape': "stadium"}},
    {tag: "port", index: 5, width: 0, height: 0,
      attrs: {'data-node-id': "client", 'data-port-id': "in",
        'data-port-side': "west", 'data-port-offset': "0.5"}},
    {tag: "edge", index: 6, width: 0, height: 0,
      attrs: {'data-edge-id': "call", 'data-from': "service", 'data-to': "client",
        'data-from-port': "out", 'data-to-port': "in"}}
  ], null)
let velmt_cluster = velmt_result.clusters[0]
let velmt_service = item_by_id(velmt_result.nodes, "service")
let velmt_client = item_by_id(velmt_result.nodes, "client")
let velmt_edge = item_by_id(velmt_result.edges, "call")
let velmt_start = velmt_edge.points[0]
let velmt_finish = velmt_edge.points[len(velmt_edge.points) - 1]
let velmt_cluster_place = placement_by_index(velmt_result.placements, 0)
let velmt_label_place = placement_by_index(velmt_result.placements, 1)
let velmt_port_place = placement_by_index(velmt_result.placements, 3)

let default_port_result = layout.compute({
  nodes: [
    {id: "left", width: 40, height: 30, ports: [{id: "auto"}]},
    {id: "right", width: 40, height: 30, ports: [{id: "auto"}]}
  ],
  edges: [{id: "auto", from: "left", to: "right",
    from_port: "auto", to_port: "auto"}]
}, {direction: "LR", rank_sep: 80})
let default_left = item_by_id(default_port_result.nodes, "left")
let default_right = item_by_id(default_port_result.nodes, "right")
let default_points = default_port_result.edges[0].points
let default_start = default_points[0]
let default_finish = default_points[len(default_points) - 1]

{
  canonical_valid: canonical.valid,
  html_counts: [len(html_clusters), len(html_cluster_labels), len(html_ports),
    len(html_nodes), len(html_edges)],
  html_ports: [for (port in html_ports)
    [port["data-node-id"], port["data-port-id"], port["data-port-side"]]],
  html_shape: len(hex_nodes) == 1 and contains(string(hex_nodes[0].style), "clip-path:polygon"),
  cluster_containment: contains_node(outer, a) and contains_node(outer, b) and
    contains_node(inner, b) and contains_cluster(outer, inner),
  measured_labels: outer.label_y >= outer.y and inner.label_y >= inner.y and
    outer.label_y < a.y - a.height / 2.0 and inner.label_y < b.y - b.height / 2.0,
  named_ports: abs(start.x - (b.x + b.width / 2.0)) < 0.001 and
    abs(start.y - (b.y - b.height / 2.0 + b.height * 0.75)) < 0.001 and
    abs(finish.x - (c.x - c.width / 2.0)) < 0.001 and
    abs(finish.y - c.y) < 0.001,
  compound_routes: len(bc1.points) > 3 and len(bc2.points) > 3 and
    points_differ(bc1.points, bc2.points),
  parallel_ports: abs(pq_start.x - (p.x + p.width / 2.0)) < 0.001 and
    abs(pq_start.y - (p.y - p.height / 2.0 + p.height * 0.2)) < 0.001 and
    abs(pq_finish.x - (q.x - q.width / 2.0)) < 0.001 and
    abs(pq_finish.y - (q.y - q.height / 2.0 + q.height * 0.6)) < 0.001,
  loop_ports: abs(loop_start.x - (p.x + p.width / 2.0)) < 0.001 and
    abs(loop_start.y - (p.y - p.height / 2.0 + p.height * 0.2)) < 0.001 and
    abs(loop_finish.x - (p.x - p.width / 2.0)) < 0.001 and
    abs(loop_finish.y - (p.y - p.height / 2.0 + p.height * 0.8)) < 0.001,
  velmt_flow: contains_node(velmt_cluster, velmt_service) and
    abs(velmt_start.x - (velmt_service.x + velmt_service.width / 2.0)) < 0.001 and
    abs(velmt_finish.x - (velmt_client.x - velmt_client.width / 2.0)) < 0.001 and
    velmt_cluster_place.x == 0.0 and velmt_cluster_place.y == 0.0 and
    velmt_port_place.x == 0.0 and velmt_port_place.y == 0.0 and
    abs(velmt_label_place.x - velmt_cluster.label_x) < 0.001 and
    abs(velmt_label_place.y - velmt_cluster.label_y) < 0.001,
  port_defaults: abs(default_start.x - (default_left.x + default_left.width / 2.0)) < 0.001 and
    abs(default_start.y - default_left.y) < 0.001 and
    abs(default_finish.x - (default_right.x - default_right.width / 2.0)) < 0.001 and
    abs(default_finish.y - default_right.y) < 0.001,
  shapes: [for (node in result.nodes) node.shape],
  paint: len(cluster_layers) == 2 and
    cluster_layers[0].content[0]["data-graph-role"] == "cluster" and
    cluster_layers[0].content[0]["data-cluster-id"] == "outer"
}
